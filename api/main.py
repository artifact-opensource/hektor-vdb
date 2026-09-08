"""
Vector Studio REST API Server
Production-ready FastAPI wrapper around the C++ pyvdb backend

Features:
- Full CRUD operations for collections and documents
- Semantic search with filters
- Authentication (JWT)
- Rate limiting
- CORS support
- Prometheus metrics
- Health checks
- OpenAPI documentation
"""

from fastapi import FastAPI, HTTPException, Depends, status, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.middleware.gzip import GZipMiddleware
from fastapi.security import HTTPBearer, HTTPAuthorizationCredentials
from pydantic import BaseModel, Field, validator
from typing import List, Dict, Any, Optional
from datetime import datetime, timedelta
import logging
from logging.handlers import RotatingFileHandler
import time
import os
import sys
import json
from pathlib import Path
from contextlib import asynccontextmanager

# Prometheus metrics
from prometheus_client import Counter, Histogram, Gauge, generate_latest, CONTENT_TYPE_LATEST
from starlette.responses import Response

# Rate limiting
from slowapi import Limiter, _rate_limit_exceeded_handler
from slowapi.util import get_remote_address
from slowapi.errors import RateLimitExceeded

# JWT authentication
import jwt
from passlib.context import CryptContext

# Import the C++ backend via pybind11
try:
    import pyvdb
except ImportError:
    print("ERROR: pyvdb module not found. Please build the Python bindings first.")
    print("Run: cmake --build build --target pyvdb")
    sys.exit(1)

# ============================================================================
# Configuration
# ============================================================================

class Settings:
    """Application settings from environment variables"""
    APP_NAME = "Vector Studio API"
    VERSION = "2.0.0"
    DEBUG = os.getenv("DEBUG", "false").lower() == "true"
    
    # Database
    DB_PATH = os.getenv("VDB_PATH", "./data/vectors")
    
    # Security
    SECRET_KEY = os.getenv("SECRET_KEY")
    JWT_ALGORITHM = "HS256"
    ACCESS_TOKEN_EXPIRE_MINUTES = int(os.getenv("ACCESS_TOKEN_EXPIRE_MINUTES", "60"))
    STRICT_SECURITY = os.getenv("STRICT_SECURITY", "true").lower() == "true"
    ADMIN_USERNAME = os.getenv("API_ADMIN_USERNAME")
    ADMIN_PASSWORD = os.getenv("API_ADMIN_PASSWORD")
    
    # CORS
    CORS_ORIGINS = [origin.strip() for origin in os.getenv("CORS_ORIGINS", "").split(",") if origin.strip()]
    
    # Rate limiting
    RATE_LIMIT_ENABLED = os.getenv("RATE_LIMIT_ENABLED", "true").lower() == "true"
    RATE_LIMIT_DEFAULT = os.getenv("RATE_LIMIT_DEFAULT", "100/minute")
    
    # Server
    HOST = os.getenv("HOST", "0.0.0.0")
    PORT = int(os.getenv("PORT", "8080"))
    WORKERS = int(os.getenv("WORKERS", "4"))
    
    # Logging
    LOG_LEVEL = os.getenv("LOG_LEVEL", "INFO").upper()
    LOG_FILE_PATH = os.getenv("LOG_FILE_PATH", "./logs/api.log")
    LOG_MAX_BYTES = int(os.getenv("LOG_MAX_BYTES", str(10 * 1024 * 1024)))
    LOG_BACKUP_COUNT = int(os.getenv("LOG_BACKUP_COUNT", "5"))

settings = Settings()

INSECURE_SECRET_DEFAULTS = {
    "",
    "CHANGE_ME_IN_PRODUCTION",
    "change-me-in-production",
    "changeme",
    "secret",
}


def validate_security_settings() -> None:
    """Validate required production security settings."""
    problems: List[str] = []

    if not settings.SECRET_KEY or settings.SECRET_KEY in INSECURE_SECRET_DEFAULTS:
        problems.append("SECRET_KEY must be explicitly set to a strong non-default value")

    if not settings.CORS_ORIGINS or "*" in settings.CORS_ORIGINS:
        problems.append("CORS_ORIGINS must be set to explicit trusted origins and must not contain '*'")

    if not settings.ADMIN_USERNAME or not settings.ADMIN_PASSWORD:
        problems.append("API_ADMIN_USERNAME and API_ADMIN_PASSWORD must be set")

    if problems and settings.STRICT_SECURITY:
        raise RuntimeError("Invalid security configuration: " + "; ".join(problems))

# ============================================================================
# Logging Setup
# ============================================================================

log_file_path = Path(settings.LOG_FILE_PATH).expanduser()
log_file_path.parent.mkdir(parents=True, exist_ok=True)

logging.basicConfig(
    level=getattr(logging, settings.LOG_LEVEL),
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    handlers=[
        logging.StreamHandler(),
        RotatingFileHandler(
            str(log_file_path),
            maxBytes=settings.LOG_MAX_BYTES,
            backupCount=settings.LOG_BACKUP_COUNT
        )
    ]
)
logger = logging.getLogger(__name__)

# ============================================================================
# Prometheus Metrics
# ============================================================================

request_count = Counter(
    'vdb_api_requests_total',
    'Total API requests',
    ['method', 'endpoint', 'status']
)

request_duration = Histogram(
    'vdb_api_request_duration_seconds',
    'Request duration in seconds',
    ['method', 'endpoint']
)

active_connections = Gauge(
    'vdb_api_active_connections',
    'Number of active connections'
)

db_operations = Counter(
    'vdb_operations_total',
    'Total database operations',
    ['operation', 'collection']
)

db_operation_duration = Histogram(
    'vdb_operation_duration_seconds',
    'Database operation duration',
    ['operation']
)

vector_count = Gauge(
    'vdb_vectors_total',
    'Total number of vectors',
    ['collection']
)

# ============================================================================
# Database Manager
# ============================================================================

class DatabaseManager:
    """Manages the C++ vector database instance"""
    
    def __init__(self):
        self.db: Optional[pyvdb.VectorDatabase] = None
        self._initialized = False
        self.collections: Dict[str, Dict[str, Any]] = {}
    
    def initialize(self):
        """Initialize the database connection"""
        if self._initialized:
            return
        
        try:
            logger.info(f"Initializing database at {settings.DB_PATH}")
            Path(settings.DB_PATH).mkdir(parents=True, exist_ok=True)
            
            # Create or open database
            self.db = pyvdb.create_gold_standard_db(settings.DB_PATH)
            self.db.init()
            
            self._initialized = True
            logger.info("Database initialized successfully")
            
            # Update metrics
            self._update_metrics()
            
        except Exception as e:
            logger.error(f"Failed to initialize database: {e}")
            raise
    
    def _update_metrics(self):
        """Update Prometheus metrics from database stats"""
        try:
            stats = self.db.stats()
            total_vectors = stats.get('total_vectors', 0) if isinstance(stats, dict) else getattr(stats, 'total_vectors', 0)
            vector_count.labels(collection="all").set(total_vectors)
        except Exception as e:
            logger.warning(f"Failed to update metrics: {e}")
    
    def get_db(self) -> pyvdb.VectorDatabase:
        """Get the database instance"""
        if not self._initialized:
            self.initialize()
        return self.db

    def create_collection(self, name: str, dimension: int, metric: str) -> Dict[str, Any]:
        """Create tracked collection metadata."""
        if name in self.collections:
            raise ValueError(f"Collection '{name}' already exists")

        collection = {
            "name": name,
            "dimension": dimension,
            "metric": metric,
            "document_count": 0,
            "created_at": datetime.utcnow().isoformat(),
            "document_ids": set(),
        }
        self.collections[name] = collection
        return collection

    def get_collection(self, name: str) -> Optional[Dict[str, Any]]:
        """Get a collection by name."""
        return self.collections.get(name)

    def list_collections(self) -> List[Dict[str, Any]]:
        """List tracked collections."""
        return sorted(
            [
                {
                    "name": col["name"],
                    "dimension": col["dimension"],
                    "metric": col["metric"],
                    "document_count": col["document_count"],
                    "created_at": col["created_at"],
                }
                for col in self.collections.values()
            ],
            key=lambda c: c["name"]
        )

    def add_document_to_collection(self, name: str, vector_id: str) -> None:
        """Track document id for a collection."""
        collection = self.get_collection(name)
        if not collection:
            raise ValueError(f"Collection '{name}' not found")
        collection["document_ids"].add(vector_id)
        collection["document_count"] = len(collection["document_ids"])

    def delete_collection(self, name: str) -> None:
        """Delete a collection and remove tracked vectors."""
        collection = self.get_collection(name)
        if not collection:
            raise ValueError(f"Collection '{name}' not found")

        db = self.get_db()
        for vector_id in list(collection["document_ids"]):
            try:
                db.remove(int(vector_id))
            except Exception as e:
                logger.warning(f"Failed to remove vector {vector_id} while deleting collection '{name}': {e}")

        del self.collections[name]

db_manager = DatabaseManager()

# ============================================================================
# Authentication
# ============================================================================

pwd_context = CryptContext(schemes=["bcrypt"], deprecated="auto")
security = HTTPBearer()

def _build_users_db() -> Dict[str, Dict[str, str]]:
    """Build user store from environment-provided credentials."""
    users: Dict[str, Dict[str, str]] = {}
    if settings.ADMIN_USERNAME and settings.ADMIN_PASSWORD:
        users[settings.ADMIN_USERNAME] = {
            "username": settings.ADMIN_USERNAME,
            "hashed_password": pwd_context.hash(settings.ADMIN_PASSWORD),
            "role": "admin"
        }
    elif not settings.STRICT_SECURITY:
        users["admin"] = {
            "username": "admin",
            "hashed_password": pwd_context.hash("admin123"),
            "role": "admin"
        }
    return users


USERS_DB = _build_users_db()

def create_access_token(data: dict, expires_delta: Optional[timedelta] = None):
    """Create JWT access token"""
    to_encode = data.copy()
    expire = datetime.utcnow() + (expires_delta or timedelta(minutes=15))
    to_encode.update({"exp": expire})
    encoded_jwt = jwt.encode(to_encode, settings.SECRET_KEY, algorithm=settings.JWT_ALGORITHM)
    return encoded_jwt

def verify_token(credentials: HTTPAuthorizationCredentials = Depends(security)):
    """Verify JWT token"""
    try:
        token = credentials.credentials
        payload = jwt.decode(token, settings.SECRET_KEY, algorithms=[settings.JWT_ALGORITHM])
        username: str = payload.get("sub")
        if username is None:
            raise HTTPException(status_code=401, detail="Invalid authentication credentials")
        return username
    except jwt.ExpiredSignatureError:
        raise HTTPException(status_code=401, detail="Token has expired")
    except jwt.InvalidTokenError:
        raise HTTPException(status_code=401, detail="Could not validate credentials")


DOCUMENT_TYPE_MAP = {
    "journal": pyvdb.DocumentType.Journal,
    "chart": pyvdb.DocumentType.Chart,
    "catalyst_watchlist": pyvdb.DocumentType.CatalystWatchlist,
    "institutional_matrix": pyvdb.DocumentType.InstitutionalMatrix,
    "economic_calendar": pyvdb.DocumentType.EconomicCalendar,
    "weekly_rundown": pyvdb.DocumentType.WeeklyRundown,
    "3m_report": pyvdb.DocumentType.ThreeMonthReport,
    "1y_report": pyvdb.DocumentType.OneYearReport,
    "monthly_report": pyvdb.DocumentType.MonthlyReport,
    "yearly_report": pyvdb.DocumentType.YearlyReport,
    "premarket": pyvdb.DocumentType.PreMarket,
    "unknown": pyvdb.DocumentType.Unknown,
}


def _metadata_from_request(collection_name: str, content: str, metadata: Dict[str, Any], document_type: Optional[str]) -> pyvdb.Metadata:
    """Convert API metadata payload into native pyvdb.Metadata."""
    meta = pyvdb.Metadata()

    normalized_type = str(document_type or metadata.get("type") or "unknown").strip().lower()
    meta.type = DOCUMENT_TYPE_MAP.get(normalized_type, pyvdb.DocumentType.Unknown)
    meta.date = str(metadata.get("date", datetime.utcnow().date().isoformat()))
    meta.asset = str(metadata.get("asset", ""))
    meta.bias = str(metadata.get("bias", ""))
    meta.source_file = f"api:{collection_name}"

    if "gold_price" in metadata:
        try:
            meta.gold_price = float(metadata["gold_price"])
        except (TypeError, ValueError):
            pass
    if "silver_price" in metadata:
        try:
            meta.silver_price = float(metadata["silver_price"])
        except (TypeError, ValueError):
            pass
    if "gsr" in metadata:
        try:
            meta.gsr = float(metadata["gsr"])
        except (TypeError, ValueError):
            pass
    if "dxy" in metadata:
        try:
            meta.dxy = float(metadata["dxy"])
        except (TypeError, ValueError):
            pass
    if "vix" in metadata:
        try:
            meta.vix = float(metadata["vix"])
        except (TypeError, ValueError):
            pass
    if "yield_10y" in metadata:
        try:
            meta.yield_10y = float(metadata["yield_10y"])
        except (TypeError, ValueError):
            pass

    user_metadata = dict(metadata)
    user_metadata["collection"] = collection_name
    user_metadata["content"] = content
    meta.extra_json = json.dumps(user_metadata, ensure_ascii=False)
    return meta


def _metadata_to_response(metadata: Any) -> Dict[str, Any]:
    """Convert native metadata object to API response dict."""
    if metadata is None:
        return {}

    response = {
        "id": getattr(metadata, "id", 0),
        "type": str(getattr(metadata, "type", "unknown")),
        "date": getattr(metadata, "date", ""),
        "source_file": getattr(metadata, "source_file", ""),
        "asset": getattr(metadata, "asset", ""),
        "bias": getattr(metadata, "bias", ""),
    }
    extra_json = getattr(metadata, "extra_json", "")
    if extra_json:
        try:
            extra = json.loads(extra_json)
            if isinstance(extra, dict):
                response.update(extra)
        except json.JSONDecodeError:
            pass
    return response

# ============================================================================
# Pydantic Models
# ============================================================================

class Token(BaseModel):
    access_token: str
    token_type: str

class LoginRequest(BaseModel):
    username: str
    password: str

class CollectionCreate(BaseModel):
    name: str = Field(..., min_length=1, max_length=100)
    dimension: int = Field(default=1536, ge=1, le=4096)
    metric: str = Field(default="cosine")
    
    @validator('metric')
    def validate_metric(cls, v):
        allowed = ['cosine', 'euclidean', 'dot_product']
        if v not in allowed:
            raise ValueError(f"Metric must be one of {allowed}")
        return v

class CollectionInfo(BaseModel):
    name: str
    dimension: int
    metric: str
    document_count: int
    created_at: Optional[str] = None

class DocumentAdd(BaseModel):
    content: str = Field(..., min_length=1)
    metadata: Dict[str, Any] = Field(default_factory=dict)
    document_type: Optional[str] = "general"

class DocumentBatchAdd(BaseModel):
    documents: List[DocumentAdd]

class SearchRequest(BaseModel):
    query: str = Field(..., min_length=1)
    k: int = Field(default=10, ge=1, le=100)
    filters: Optional[Dict[str, Any]] = None

class SearchResult(BaseModel):
    id: str
    score: float
    content: Optional[str] = None
    metadata: Dict[str, Any]

class HealthResponse(BaseModel):
    status: str
    version: str
    database: str
    uptime_seconds: float

class StatsResponse(BaseModel):
    total_vectors: int
    memory_usage_bytes: int
    index_size: int
    collections: int

# ============================================================================
# Rate Limiting
# ============================================================================

limiter = Limiter(key_func=get_remote_address)

# ============================================================================
# Application Lifecycle
# ============================================================================

start_time = time.time()

@asynccontextmanager
async def lifespan(app: FastAPI):
    """Application lifespan manager"""
    # Startup
    logger.info("Starting Vector Studio API...")
    validate_security_settings()
    db_manager.initialize()
    logger.info("API ready to accept requests")
    
    yield
    
    # Shutdown
    logger.info("Shutting down Vector Studio API...")
    if db_manager.db:
        try:
            db_manager.db.sync()
            logger.info("Database synced successfully")
        except Exception as e:
            logger.error(f"Error syncing database: {e}")

# ============================================================================
# FastAPI Application
# ============================================================================

app = FastAPI(
    title=settings.APP_NAME,
    version=settings.VERSION,
    description="Production-ready vector database REST API",
    docs_url="/docs",
    redoc_url="/redoc",
    lifespan=lifespan
)

# Add rate limiting
app.state.limiter = limiter
app.add_exception_handler(RateLimitExceeded, _rate_limit_exceeded_handler)

# Add CORS middleware
app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.CORS_ORIGINS,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# Add GZip compression
app.add_middleware(GZipMiddleware, minimum_size=1000)

# ============================================================================
# Middleware
# ============================================================================

@app.middleware("http")
async def metrics_middleware(request: Request, call_next):
    """Track request metrics"""
    active_connections.inc()
    start = time.time()
    
    try:
        response = await call_next(request)
        duration = time.time() - start
        
        # Record metrics
        request_count.labels(
            method=request.method,
            endpoint=request.url.path,
            status=response.status_code
        ).inc()
        
        request_duration.labels(
            method=request.method,
            endpoint=request.url.path
        ).observe(duration)
        
        return response
    finally:
        active_connections.dec()

# ============================================================================
# Health & Metrics Endpoints
# ============================================================================

@app.get("/health", response_model=HealthResponse, tags=["System"])
async def health_check():
    """Health check endpoint"""
    try:
        db = db_manager.get_db()
        db_status = "healthy"
    except Exception as e:
        logger.error(f"Health check failed: {e}")
        db_status = "unhealthy"
        raise HTTPException(status_code=503, detail="Database unavailable")
    
    return {
        "status": "healthy",
        "version": settings.VERSION,
        "database": db_status,
        "uptime_seconds": time.time() - start_time
    }

@app.get("/metrics", tags=["System"])
async def metrics():
    """Prometheus metrics endpoint"""
    return Response(content=generate_latest(), media_type=CONTENT_TYPE_LATEST)

@app.get("/stats", response_model=StatsResponse, tags=["System"])
async def get_stats(username: str = Depends(verify_token)):
    """Get database statistics"""
    db = db_manager.get_db()
    stats = db.stats()
    total_vectors = stats.get('total_vectors', 0) if isinstance(stats, dict) else getattr(stats, 'total_vectors', 0)
    memory_usage_bytes = stats.get('memory_usage_bytes', 0) if isinstance(stats, dict) else getattr(stats, 'memory_usage_bytes', 0)
    index_size = stats.get('index_size', 0) if isinstance(stats, dict) else getattr(stats, 'index_size_bytes', 0)
    collections = len(db_manager.collections)
    
    return {
        "total_vectors": total_vectors,
        "memory_usage_bytes": memory_usage_bytes,
        "index_size": index_size,
        "collections": collections
    }

# ============================================================================
# Authentication Endpoints
# ============================================================================

@app.post("/auth/login", response_model=Token, tags=["Authentication"])
@limiter.limit("5/minute")
async def login(request: Request, login_data: LoginRequest):
    """Login and get access token"""
    if not USERS_DB:
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail="Authentication is not configured on this server"
        )

    user = USERS_DB.get(login_data.username)
    
    if not user or not pwd_context.verify(login_data.password, user["hashed_password"]):
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Incorrect username or password"
        )
    
    access_token_expires = timedelta(minutes=settings.ACCESS_TOKEN_EXPIRE_MINUTES)
    access_token = create_access_token(
        data={"sub": user["username"], "role": user["role"]},
        expires_delta=access_token_expires
    )
    
    return {"access_token": access_token, "token_type": "bearer"}

# ============================================================================
# Collection Endpoints
# ============================================================================

@app.post("/collections", response_model=CollectionInfo, tags=["Collections"])
@limiter.limit(settings.RATE_LIMIT_DEFAULT)
async def create_collection(
    request: Request,
    collection: CollectionCreate,
    username: str = Depends(verify_token)
):
    """Create a new collection"""
    start = time.time()
    
    try:
        db_manager.get_db()
        logger.info(f"Creating collection: {collection.name}")
        created = db_manager.create_collection(collection.name, collection.dimension, collection.metric)

        db_operations.labels(operation="create_collection", collection=collection.name).inc()
        return {k: v for k, v in created.items() if k != "document_ids"}
    except Exception as e:
        logger.error(f"Failed to create collection: {e}")
        raise HTTPException(status_code=500, detail=str(e))
    finally:
        db_operation_duration.labels(operation="create_collection").observe(time.time() - start)

@app.get("/collections", response_model=List[CollectionInfo], tags=["Collections"])
@limiter.limit(settings.RATE_LIMIT_DEFAULT)
async def list_collections(
    request: Request,
    username: str = Depends(verify_token)
):
    """List all collections"""
    return db_manager.list_collections()

@app.delete("/collections/{collection_name}", tags=["Collections"])
@limiter.limit(settings.RATE_LIMIT_DEFAULT)
async def delete_collection(
    request: Request,
    collection_name: str,
    username: str = Depends(verify_token)
):
    """Delete a collection"""
    try:
        db_manager.get_db()
        logger.info(f"Deleting collection: {collection_name}")
        db_manager.delete_collection(collection_name)
        db_operations.labels(operation="delete_collection", collection=collection_name).inc()
        
        return {"message": f"Collection {collection_name} deleted successfully"}
    except Exception as e:
        logger.error(f"Failed to delete collection: {e}")
        raise HTTPException(status_code=500, detail=str(e))

# ============================================================================
# Document Endpoints
# ============================================================================

@app.post("/collections/{collection_name}/documents", tags=["Documents"])
@limiter.limit(settings.RATE_LIMIT_DEFAULT)
async def add_document(
    request: Request,
    collection_name: str,
    document: DocumentAdd,
    username: str = Depends(verify_token)
):
    """Add a single document to a collection"""
    start = time.time()
    
    try:
        db = db_manager.get_db()
        if not db_manager.get_collection(collection_name):
            raise HTTPException(status_code=404, detail=f"Collection '{collection_name}' not found")

        # Add text with metadata
        native_metadata = _metadata_from_request(
            collection_name=collection_name,
            content=document.content,
            metadata=document.metadata,
            document_type=document.document_type
        )
        result = db.add_text(
            document.content,
            native_metadata
        )
        db_manager.add_document_to_collection(collection_name, str(result))
        
        db_operations.labels(operation="add_document", collection=collection_name).inc()
        
        return {
            "id": str(result),
            "message": "Document added successfully"
        }
    except Exception as e:
        logger.error(f"Failed to add document: {e}")
        raise HTTPException(status_code=500, detail=str(e))
    finally:
        db_operation_duration.labels(operation="add_document").observe(time.time() - start)

@app.post("/collections/{collection_name}/documents/batch", tags=["Documents"])
@limiter.limit("10/minute")
async def add_documents_batch(
    request: Request,
    collection_name: str,
    batch: DocumentBatchAdd,
    username: str = Depends(verify_token)
):
    """Add multiple documents in batch"""
    start = time.time()
    
    try:
        db = db_manager.get_db()
        if not db_manager.get_collection(collection_name):
            raise HTTPException(status_code=404, detail=f"Collection '{collection_name}' not found")

        added_ids = []
        
        for doc in batch.documents:
            native_metadata = _metadata_from_request(
                collection_name=collection_name,
                content=doc.content,
                metadata=doc.metadata,
                document_type=doc.document_type
            )
            result = db.add_text(doc.content, native_metadata)
            added_ids.append(str(result))
            db_manager.add_document_to_collection(collection_name, str(result))
        
        db_operations.labels(operation="add_batch", collection=collection_name).inc()
        
        return {
            "ids": added_ids,
            "count": len(added_ids),
            "message": f"Added {len(added_ids)} documents successfully"
        }
    except Exception as e:
        logger.error(f"Failed to add batch: {e}")
        raise HTTPException(status_code=500, detail=str(e))
    finally:
        db_operation_duration.labels(operation="add_batch").observe(time.time() - start)

# ============================================================================
# Search Endpoints
# ============================================================================

@app.post("/collections/{collection_name}/search", response_model=List[SearchResult], tags=["Search"])
@limiter.limit(settings.RATE_LIMIT_DEFAULT)
async def search(
    request: Request,
    collection_name: str,
    search_request: SearchRequest,
    username: str = Depends(verify_token)
):
    """Semantic search in a collection"""
    start = time.time()
    
    try:
        db = db_manager.get_db()
        
        # Perform search
        if not db_manager.get_collection(collection_name):
            raise HTTPException(status_code=404, detail=f"Collection '{collection_name}' not found")

        results = db.search(search_request.query, search_request.k * 5)
        
        db_operations.labels(operation="search", collection=collection_name).inc()
        
        # Convert results to response format
        search_results = []
        for r in results:
            metadata_dict = _metadata_to_response(getattr(r, "metadata", None))
            if metadata_dict.get("collection") != collection_name:
                continue

            search_results.append({
                "id": str(r.id),
                "score": float(r.score),
                "content": metadata_dict.get("content"),
                "metadata": metadata_dict
            })
            if len(search_results) >= search_request.k:
                break
        
        return search_results
    except Exception as e:
        logger.error(f"Search failed: {e}")
        raise HTTPException(status_code=500, detail=str(e))
    finally:
        db_operation_duration.labels(operation="search").observe(time.time() - start)

# ============================================================================
# Main Entry Point
# ============================================================================

if __name__ == "__main__":
    import uvicorn
    
    uvicorn.run(
        "main:app",
        host=settings.HOST,
        port=settings.PORT,
        workers=settings.WORKERS,
        log_level=settings.LOG_LEVEL.lower(),
        reload=settings.DEBUG
    )
