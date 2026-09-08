import { useEffect } from 'react'
import themeData from './data/theme.json'
import pricingData from './data/pricing.json'
import modelingData from './data/modeling.json'

interface ThemeData {
  name: string
  colors: Record<string, string>
}

interface PricingPlan {
  id: string
  name: string
  priceLabel: string
  billing: string
  annualPriceUsd: number
  audience: string
  summary: string
  commitments: string[]
}

interface PricingData {
  benchmarks: {
    tcoSavings: string
    latency: string
    recall: string
  }
  plans: PricingPlan[]
  cloudProjections: Array<{
    name: string
    priceLabel: string
    notes: string
  }>
}

interface ModelingData {
  valueProposition: Array<{
    label: string
    value: string
    context: string
  }>
  segments: Array<{
    name: string
    focus: string
    trigger: string
  }>
  deploymentModels: Array<{
    name: string
    fit: string
    notes: string
  }>
  useCases: Array<{
    label: string
    motion: string
  }>
}

const theme = themeData as ThemeData
const pricing = pricingData as PricingData
const modeling = modelingData as ModelingData

const signupEmailHref = (() => {
  const subject = 'Hektor signup request'
  const body = [
    'Hi Artifact Virtual team,',
    '',
    'I want to get started with HEKTOR.',
    '',
    'Company:',
    'Role:',
    'Primary use case:',
    'Estimated vector scale:',
    'Deployment preference:',
    'Timeline:',
    '',
    'Please contact me with next steps.',
  ].join('\n')

  return `mailto:support@artifactvirtual.com?subject=${encodeURIComponent(subject)}&body=${encodeURIComponent(body)}`
})()

const featureHighlights = [
  {
    title: 'Vector search built for production pressure',
    description:
      'Run SIMD-optimized similarity search, HNSW indexing, and hybrid retrieval in a stack built for real workloads instead of demo traffic.',
  },
  {
    title: 'Local embeddings without API dependency',
    description:
      'Generate text and image embeddings with ONNX Runtime locally to reduce latency, cut vendor exposure, and keep sensitive data under your control.',
  },
  {
    title: 'Multimodal and RAG-ready from day one',
    description:
      'Support retrieval pipelines across text, image, document, and structured data workflows with BM25 fusion, chunking strategies, and framework adapters.',
  },
  {
    title: 'Deployment paths for self-hosted and future cloud',
    description:
      'Start with the MIT core, add enterprise support when needed, and plan for managed Hektor Cloud without replatforming your stack.',
  },
]

const capabilityColumns = [
  {
    heading: 'Core platform',
    items: [
      'SIMD-optimized cosine and Euclidean search',
      'HNSW indexing for low-latency nearest-neighbor retrieval',
      'Memory-mapped storage for efficient large-scale workloads',
      'Rich metadata filtering and universal ingestion adapters',
    ],
  },
  {
    heading: 'AI workflow coverage',
    items: [
      'Hybrid search with BM25 and five fusion methods',
      'RAG engine with fixed, sentence, paragraph, semantic, and recursive chunking',
      'Local ONNX text and image embeddings',
      'Python bindings plus CLI and API integration surfaces',
    ],
  },
  {
    heading: 'Operational readiness',
    items: [
      'Docker, Kubernetes, and bare-metal deployment options',
      'OpenTelemetry tracing, Prometheus metrics, and eBPF observability',
      'Distributed architecture with replication and sharding modes',
      'Support paths for migrations, architecture review, and incident response',
    ],
  },
]

const proofPoints = [
  {
    label: 'Performance envelope',
    value: pricing.benchmarks.latency,
    detail: 'Documented p99 latency at 1M vectors for production retrieval workloads.',
  },
  {
    label: 'Recall at scale',
    value: pricing.benchmarks.recall,
    detail: 'Benchmarked retrieval quality for large-scale semantic search and multimodal systems.',
  },
  {
    label: 'Cost position',
    value: pricing.benchmarks.tcoSavings,
    detail: 'Modeled savings versus managed vector database alternatives for self-hosted operators.',
  },
  {
    label: 'Deployment range',
    value: '1M to 1B+ vectors',
    detail: 'Single-node entry points through distributed billion-scale deployments.',
  },
]

function App() {
  useEffect(() => {
    const root = document.documentElement
    Object.entries(theme.colors).forEach(([token, value]) => {
      const cssVariable = token.replace(/[A-Z]/g, (letter) => `-${letter.toLowerCase()}`)
      root.style.setProperty(`--${cssVariable}`, value)
    })

    const handlePointerMove = (event: MouseEvent) => {
      const x = `${(event.clientX / window.innerWidth) * 100}%`
      const y = `${(event.clientY / window.innerHeight) * 100}%`
      root.style.setProperty('--mx', x)
      root.style.setProperty('--my', y)
    }

    window.addEventListener('mousemove', handlePointerMove)
    return () => window.removeEventListener('mousemove', handlePointerMove)
  }, [])

  return (
    <div className="app-shell">
      <div className="grain-layer" />
      <div className="orb orb-cyan" />
      <div className="orb orb-violet" />

      <header className="site-header">
        <div className="brand-lockup">
          <p className="eyebrow">High-fidelity vector infrastructure</p>
          <h1>HEKTOR</h1>
        </div>
        <nav className="site-nav" aria-label="Primary">
          <a href="#platform">Platform</a>
          <a href="#use-cases">Use cases</a>
          <a href="#pricing">Pricing</a>
          <a href="#signup">Signup</a>
        </nav>
      </header>

      <main className="page-layout">
        <section className="hero panel">
          <div className="hero-copy">
            <p className="eyebrow">Production vector database for real AI workloads</p>
            <h2>Build faster retrieval systems with open-source infrastructure you can control.</h2>
            <p className="hero-text">
              HEKTOR gives teams SIMD-optimized vector search, hybrid retrieval, local embeddings,
              and a practical path from single-node deployments to billion-scale architectures.
            </p>
            <div className="hero-actions">
              <a className="beam-button" href="#signup">
                Start with Hektor
              </a>
              <a className="ghost-button" href="#pricing">
                Explore pricing
              </a>
            </div>
            <div className="hero-proof-strip">
              {proofPoints.map((point) => (
                <article className="proof-card" key={point.label}>
                  <p className="stat-label">{point.label}</p>
                  <strong>{point.value}</strong>
                  <span>{point.detail}</span>
                </article>
              ))}
            </div>
          </div>

          <aside className="hero-aside">
            <div className="terminal-card">
              <div className="terminal-head">
                <span />
                <span />
                <span />
              </div>
              <div className="terminal-copy">
                <p>&gt; product_mode: open-source core + enterprise support</p>
                <p>&gt; deployment_paths: self-hosted · hybrid · planned cloud</p>
                <p>&gt; observability: prometheus · opentelemetry · ebpf</p>
                <p>&gt; retrieval_modes: vector · hybrid · multimodal · rag</p>
                <p>&gt; pricing_anchor: {pricing.plans[0].priceLabel} to {pricing.plans[3]?.priceLabel}</p>
              </div>
            </div>
            <div className="database-card">
              <p className="eyebrow">Signup</p>
              <h3>Talk to the Hektor team</h3>
              <p>Use one click to compose your signup request and send it to support@artifactvirtual.com.</p>
              <a className="beam-button" href={signupEmailHref}>
                Email signup request
              </a>
            </div>
          </aside>
        </section>

        <section className="stats-grid" aria-label="Value proposition">
          {modeling.valueProposition.map((signal) => (
            <article className="panel stat-card" key={signal.label}>
              <p className="stat-label">{signal.label}</p>
              <h3>{signal.value}</h3>
              <p>{signal.context}</p>
            </article>
          ))}
        </section>

        <section className="platform-layout" id="platform">
          <article className="panel section-panel story-panel">
            <div className="section-heading">
              <p className="eyebrow">Why teams buy Hektor</p>
              <h3>A commercial story grounded in the product, not inflated marketing copy.</h3>
            </div>
            <div className="feature-grid">
              {featureHighlights.map((feature) => (
                <div className="feature-card" key={feature.title}>
                  <h4>{feature.title}</h4>
                  <p>{feature.description}</p>
                </div>
              ))}
            </div>
          </article>

          <article className="panel section-panel capability-panel">
            <div className="section-heading">
              <p className="eyebrow">Platform depth</p>
              <h3>The feature surface expected from serious vector infrastructure.</h3>
            </div>
            <div className="capability-columns">
              {capabilityColumns.map((column) => (
                <div className="capability-column" key={column.heading}>
                  <h4>{column.heading}</h4>
                  <ul>
                    {column.items.map((item) => (
                      <li key={item}>{item}</li>
                    ))}
                  </ul>
                </div>
              ))}
            </div>
          </article>
        </section>

        <section className="audience-grid" id="use-cases">
          <article className="panel section-panel">
            <div className="section-heading">
              <p className="eyebrow">Target segments</p>
              <h3>Where HEKTOR wins.</h3>
            </div>
            <div className="stack-list">
              {modeling.segments.map((segment) => (
                <div className="stack-item" key={segment.name}>
                  <h4>{segment.name}</h4>
                  <p>{segment.focus}</p>
                  <small>{segment.trigger}</small>
                </div>
              ))}
            </div>
          </article>

          <article className="panel section-panel">
            <div className="section-heading">
              <p className="eyebrow">Deployment options</p>
              <h3>Adopt Hektor on your terms.</h3>
            </div>
            <div className="stack-list">
              {modeling.deploymentModels.map((route) => (
                <div className="stack-item" key={route.name}>
                  <h4>{route.name}</h4>
                  <p>{route.fit}</p>
                  <small>{route.notes}</small>
                </div>
              ))}
            </div>
          </article>

          <article className="panel section-panel">
            <div className="section-heading">
              <p className="eyebrow">Operational use cases</p>
              <h3>Deployment priorities aligned to buyer intent.</h3>
            </div>
            <div className="stack-list">
              {modeling.useCases.map((useCase) => (
                <div className="stack-item" key={useCase.label}>
                  <h4>{useCase.label}</h4>
                  <small>{useCase.motion}</small>
                </div>
              ))}
            </div>
          </article>
        </section>

        <section className="pricing-layout" id="pricing">
          <div className="section-heading section-heading-wide">
            <div>
              <p className="eyebrow">Pricing structure</p>
              <h3>Open-source core, optional support, and a clear path to managed service.</h3>
            </div>
            <p>
              The core product is free under MIT. Commercial spend starts only when a team wants
              response SLAs, migration support, architecture review, or strategic partnership.
            </p>
          </div>

          <div className="pricing-grid">
            {pricing.plans.map((plan) => (
              <article className={`panel pricing-card ${plan.id === 'premium-support' ? 'pricing-card-featured' : ''}`} key={plan.id}>
                <p className="stat-label">{plan.audience}</p>
                <h4>{plan.name}</h4>
                <div className="price-stack">
                  <strong>{plan.priceLabel}</strong>
                  <span>{plan.billing}</span>
                </div>
                <p>{plan.summary}</p>
                <ul>
                  {plan.commitments.map((commitment) => (
                    <li key={commitment}>{commitment}</li>
                  ))}
                </ul>
              </article>
            ))}
          </div>

          <div className="cloud-grid">
            {pricing.cloudProjections.map((projection) => (
              <article className="panel cloud-card" key={projection.name}>
                <p className="eyebrow">Planned Hektor Cloud</p>
                <h4>{projection.name}</h4>
                <strong>{projection.priceLabel}</strong>
                <p>{projection.notes}</p>
              </article>
            ))}
          </div>
        </section>

        <section className="signup-layout" id="signup">
          <article className="panel section-panel signup-panel">
            <div className="section-heading">
              <p className="eyebrow">Signup</p>
              <h3>Request access and support for your Hektor deployment.</h3>
              <p>
                Send your request to support@artifactvirtual.com and include your use case,
                deployment target, and expected scale.
              </p>
            </div>
            <div className="form-actions">
              <a className="beam-button" href={signupEmailHref}>
                Compose signup email
              </a>
            </div>
          </article>
        </section>
      </main>
    </div>
  )
}

export default App
