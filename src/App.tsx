import { useEffect, useMemo, useState } from 'react'
import type { FormEvent } from 'react'
import themeData from './data/theme.json'
import pricingData from './data/pricing.json'
import modelingData from './data/modeling.json'

type SubmissionState = 'idle' | 'saving' | 'success' | 'error'

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

interface SignupRecord {
  id: string
  name: string
  email: string
  organization: string
  role: string
  planId: string
  useCase: string
  deploymentPreference: string
  vectorScale: string
  message: string
  submittedAt: string
}

interface SignupFormState {
  name: string
  email: string
  organization: string
  role: string
  planId: string
  useCase: string
  deploymentPreference: string
  vectorScale: string
  message: string
}

const theme = themeData as ThemeData
const pricing = pricingData as PricingData
const modeling = modelingData as ModelingData

const membershipDatabaseName = 'hektor-memberships'
const membershipStoreName = 'applications'

const createInitialForm = (): SignupFormState => ({
  name: '',
  email: '',
  organization: '',
  role: '',
  planId: pricing.plans[2]?.id ?? pricing.plans[0].id,
  useCase: modeling.useCases[0]?.label ?? 'Production RAG infrastructure',
  deploymentPreference: modeling.deploymentModels[0]?.name ?? 'Self-hosted core',
  vectorScale: '10M vectors',
  message: '',
})

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

const openMembershipDatabase = () =>
  new Promise<IDBDatabase>((resolve, reject) => {
    const request = window.indexedDB.open(membershipDatabaseName, 1)

    request.onupgradeneeded = () => {
      const database = request.result

      if (!database.objectStoreNames.contains(membershipStoreName)) {
        const store = database.createObjectStore(membershipStoreName, { keyPath: 'id' })
        store.createIndex('submittedAt', 'submittedAt', { unique: false })
        store.createIndex('planId', 'planId', { unique: false })
      }
    }

    request.onsuccess = () => resolve(request.result)
    request.onerror = () => reject(request.error ?? new Error('Failed to open membership database'))
  })

const readMemberships = async () => {
  const database = await openMembershipDatabase()

  return new Promise<SignupRecord[]>((resolve, reject) => {
    const transaction = database.transaction(membershipStoreName, 'readonly')
    const store = transaction.objectStore(membershipStoreName)
    const request = store.getAll()

    request.onsuccess = () => {
      const records = (request.result as SignupRecord[]).sort((left, right) =>
        right.submittedAt.localeCompare(left.submittedAt),
      )
      resolve(records)
    }

    request.onerror = () => reject(request.error ?? new Error('Failed to load memberships'))
    transaction.oncomplete = () => database.close()
    transaction.onerror = () => reject(transaction.error ?? new Error('Membership read transaction failed'))
  })
}

const storeMembership = async (record: SignupRecord) => {
  const database = await openMembershipDatabase()

  return new Promise<void>((resolve, reject) => {
    const transaction = database.transaction(membershipStoreName, 'readwrite')
    const store = transaction.objectStore(membershipStoreName)
    const request = store.put(record)

    request.onsuccess = () => undefined
    request.onerror = () => reject(request.error ?? new Error('Failed to save membership'))
    transaction.oncomplete = () => {
      database.close()
      resolve()
    }
    transaction.onerror = () => reject(transaction.error ?? new Error('Membership write transaction failed'))
  })
}

function App() {
  const [memberships, setMemberships] = useState<SignupRecord[]>([])
  const [formState, setFormState] = useState<SignupFormState>(createInitialForm)
  const [submissionState, setSubmissionState] = useState<SubmissionState>('idle')
  const [databaseStatus, setDatabaseStatus] = useState('Loading built-in signup database…')

  const plansById = useMemo(() => new Map(pricing.plans.map((plan) => [plan.id, plan])), [])

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

  useEffect(() => {
    let isMounted = true

    const loadMemberships = async () => {
      try {
        const records = await readMemberships()
        if (!isMounted) {
          return
        }

        setMemberships(records)
        setDatabaseStatus(
          records.length > 0
            ? `Built-in signup database active · ${records.length} saved ${records.length === 1 ? 'application' : 'applications'}`
            : 'Built-in signup database active · no applications yet',
        )
      } catch {
        if (!isMounted) {
          return
        }

        setDatabaseStatus('Built-in signup database unavailable in this browser session')
      }
    }

    void loadMemberships()

    return () => {
      isMounted = false
    }
  }, [])

  const membershipSummary = useMemo(() => {
    const paidApplications = memberships.filter((membership) => {
      const plan = plansById.get(membership.planId)
      return (plan?.annualPriceUsd ?? 0) > 0
    })

    const annualPipeline = paidApplications.reduce((total, membership) => {
      const plan = plansById.get(membership.planId)
      return total + (plan?.annualPriceUsd ?? 0)
    }, 0)

    const enterpriseInterest = memberships.filter((membership) =>
      ['enterprise-support', 'premium-support'].includes(membership.planId),
    ).length

    return {
      total: memberships.length,
      paidApplications: paidApplications.length,
      annualPipeline,
      enterpriseInterest,
    }
  }, [memberships, plansById])

  const recentMemberships = memberships.slice(0, 4)

  const handleSubmit = async (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault()
    setSubmissionState('saving')

    const record: SignupRecord = {
      id: `membership-${Date.now()}`,
      name: formState.name.trim(),
      email: formState.email.trim(),
      organization: formState.organization.trim(),
      role: formState.role.trim(),
      planId: formState.planId,
      useCase: formState.useCase,
      deploymentPreference: formState.deploymentPreference,
      vectorScale: formState.vectorScale,
      message: formState.message.trim(),
      submittedAt: new Date().toISOString(),
    }

    try {
      await storeMembership(record)
      const records = [record, ...memberships].sort((left, right) =>
        right.submittedAt.localeCompare(left.submittedAt),
      )
      setMemberships(records)
      setFormState(createInitialForm())
      setSubmissionState('success')
      setDatabaseStatus(
        `Built-in signup database active · ${records.length} saved ${records.length === 1 ? 'application' : 'applications'}`,
      )
    } catch {
      setSubmissionState('error')
      setDatabaseStatus('Built-in signup database unavailable in this browser session')
    }
  }

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
          <a href="#signup">Memberships</a>
        </nav>
      </header>

      <main className="page-layout">
        <section className="hero panel">
          <div className="hero-copy">
            <p className="eyebrow">Built from the product docs, not a placeholder brief</p>
            <h2>Production-grade vector search for teams that care about latency, control, and cost.</h2>
            <p className="hero-text">
              HEKTOR is an open-source vector database engineered for performance-critical retrieval,
              hybrid search, local embeddings, and billion-scale deployment paths. Start free,
              self-host with full control, and add support only when your team needs it.
            </p>
            <div className="hero-actions">
              <a className="beam-button" href="#pricing">
                Explore pricing
              </a>
              <a className="ghost-button" href="#signup">
                Apply for membership
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
              <p className="eyebrow">Membership system</p>
              <h3>Built-in signup database</h3>
              <p>
                Membership applications are stored in the browser with IndexedDB so signups persist
                between sessions without adding a third-party backend.
              </p>
              <div className="database-status">
                <span className="status-dot" />
                <strong>{databaseStatus}</strong>
              </div>
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
              <h3>Membership routing aligned to actual buyer intent.</h3>
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
              <p className="eyebrow">Membership signup</p>
              <h3>Apply for the right Hektor support path.</h3>
              <p>
                Tell us what you are building, how much scale you expect, and which operating model
                fits your team. Your submission is stored in the built-in membership database.
              </p>
            </div>

            <form className="signup-form" onSubmit={handleSubmit}>
              <div className="form-grid">
                <label>
                  Full name
                  <input
                    required
                    value={formState.name}
                    onChange={(event) => setFormState((current) => ({ ...current, name: event.target.value }))}
                    placeholder="Jordan Lee"
                  />
                </label>

                <label>
                  Work email
                  <input
                    required
                    type="email"
                    value={formState.email}
                    onChange={(event) => setFormState((current) => ({ ...current, email: event.target.value }))}
                    placeholder="jordan@company.com"
                  />
                </label>

                <label>
                  Organization
                  <input
                    required
                    value={formState.organization}
                    onChange={(event) =>
                      setFormState((current) => ({ ...current, organization: event.target.value }))
                    }
                    placeholder="Northstar AI"
                  />
                </label>

                <label>
                  Role
                  <input
                    required
                    value={formState.role}
                    onChange={(event) => setFormState((current) => ({ ...current, role: event.target.value }))}
                    placeholder="CTO, platform lead, staff engineer…"
                  />
                </label>

                <label>
                  Membership tier
                  <select
                    value={formState.planId}
                    onChange={(event) => setFormState((current) => ({ ...current, planId: event.target.value }))}
                  >
                    {pricing.plans.map((plan) => (
                      <option key={plan.id} value={plan.id}>
                        {plan.name} · {plan.priceLabel} {plan.billing}
                      </option>
                    ))}
                  </select>
                </label>

                <label>
                  Primary use case
                  <select
                    value={formState.useCase}
                    onChange={(event) => setFormState((current) => ({ ...current, useCase: event.target.value }))}
                  >
                    {modeling.useCases.map((useCase) => (
                      <option key={useCase.label} value={useCase.label}>
                        {useCase.label}
                      </option>
                    ))}
                  </select>
                </label>

                <label>
                  Deployment preference
                  <select
                    value={formState.deploymentPreference}
                    onChange={(event) =>
                      setFormState((current) => ({ ...current, deploymentPreference: event.target.value }))
                    }
                  >
                    {modeling.deploymentModels.map((route) => (
                      <option key={route.name} value={route.name}>
                        {route.name}
                      </option>
                    ))}
                  </select>
                </label>

                <label>
                  Expected scale
                  <select
                    value={formState.vectorScale}
                    onChange={(event) =>
                      setFormState((current) => ({ ...current, vectorScale: event.target.value }))
                    }
                  >
                    <option value="1M vectors">1M vectors</option>
                    <option value="10M vectors">10M vectors</option>
                    <option value="100M vectors">100M vectors</option>
                    <option value="1B+ vectors">1B+ vectors</option>
                  </select>
                </label>
              </div>

              <label>
                Project notes
                <textarea
                  rows={5}
                  value={formState.message}
                  onChange={(event) => setFormState((current) => ({ ...current, message: event.target.value }))}
                  placeholder="Share workload type, compliance constraints, migration deadlines, or architecture questions."
                />
              </label>

              <div className="form-actions">
                <button className="beam-button" type="submit" disabled={submissionState === 'saving'}>
                  {submissionState === 'saving' ? 'Saving application…' : 'Save membership application'}
                </button>
                <p className={`form-status form-status-${submissionState}`} role="status">
                  {submissionState === 'success'
                    ? 'Application saved to the built-in membership database.'
                    : submissionState === 'error'
                      ? 'Could not save the application in this browser session.'
                      : 'Your application stays in the browser via IndexedDB until you clear site data.'}
                </p>
              </div>
            </form>
          </article>

          <aside className="panel section-panel signup-sidebar">
            <div className="section-heading">
              <p className="eyebrow">Pipeline snapshot</p>
              <h3>What the local membership database is tracking.</h3>
            </div>

            <div className="pipeline-grid">
              <div className="pipeline-card">
                <span>Total applications</span>
                <strong>{membershipSummary.total}</strong>
              </div>
              <div className="pipeline-card">
                <span>Paid tier interest</span>
                <strong>{membershipSummary.paidApplications}</strong>
              </div>
              <div className="pipeline-card">
                <span>Premium or enterprise</span>
                <strong>{membershipSummary.enterpriseInterest}</strong>
              </div>
              <div className="pipeline-card">
                <span>Annual pipeline</span>
                <strong>${membershipSummary.annualPipeline.toLocaleString()}</strong>
              </div>
            </div>

            <div className="recent-list">
              {recentMemberships.length > 0 ? (
                recentMemberships.map((membership) => {
                  const plan = plansById.get(membership.planId)

                  return (
                    <article className="recent-item" key={membership.id}>
                      <div>
                        <p className="stat-label">{plan?.name ?? membership.planId}</p>
                        <h4>{membership.organization}</h4>
                      </div>
                      <p>{membership.useCase}</p>
                      <small>
                        {membership.name} · {membership.role} · {membership.vectorScale}
                      </small>
                    </article>
                  )
                })
              ) : (
                <div className="recent-item recent-item-empty">
                  <h4>No saved applications yet</h4>
                  <p>Use the form to create the first membership record in the built-in database.</p>
                </div>
              )}
            </div>
          </aside>
        </section>
      </main>
    </div>
  )
}

export default App
