import { useEffect, useMemo, useState, type ChangeEvent, type FormEvent } from 'react'

interface Metric {
  value: string
  unit?: string
  tail?: string
  label: string
}

interface Capability {
  title: string
  description: string
  icon: 'lightning' | 'layers' | 'screen'
  delayClass?: string
}

interface DepthGroup {
  heading: string
  items: string[]
}

interface Segment {
  number: string
  title: string
  description: string
  lead: string
  delayClass?: string
}

interface Deployment {
  title: string
  subtitle: string
  description: string
  pills: string[]
  note: string
  delayClass?: string
}

interface UseCase {
  number: string
  title: string
  description: string
  lead: string
  delayClass?: string
}

interface PricingCard {
  label: string
  name: string
  price?: string
  pricePrefix?: string
  priceSuffix?: string
  freeText?: string
  features: string[]
  buttonLabel: string
  buttonVariant: 'solid' | 'outline'
  plannedBadge?: string
  featured?: boolean
  planned?: boolean
}

interface FormData {
  name: string
  email: string
  company: string
  useCase: string
  deployment: string
  scale: string
  message: string
}

const heroMetrics: Metric[] = [
  {
    value: '2.9',
    unit: 'ms',
    tail: 'p99',
    label: 'Documented latency at 1M vectors for production retrieval workloads',
  },
  {
    value: '96.8',
    unit: '–98.1%',
    tail: 'recall',
    label: 'Benchmark retrieval quality for large-scale semantic search and multimodal systems',
  },
  {
    value: '60',
    unit: '–80%',
    tail: 'lower TCO',
    label: 'Modeled savings versus managed vector database alternatives for self-hosted operators',
  },
]

const performanceCards = [
  {
    number: '2.9ms',
    highlight: 'ms',
    description: 'p99 latency at 1M vectors for production retrieval workloads',
    source: 'Documented benchmark · Production-grade',
  },
  {
    number: '1B+',
    highlight: '+',
    description:
      'Qualification motion escalates accounts from pilot to billion-scale architecture with the same core engine',
    source: 'Scale range · 1M to 1B+ vectors',
  },
  {
    number: '60–80%',
    highlight: '%',
    description: 'Lower TCO using support memberships and self-hosted savings as the primary commercial lever',
    source: 'Modeled savings · vs. managed alternatives',
  },
]

const capabilities: Capability[] = [
  {
    title: 'Vector Search Built for Production Pressure',
    description:
      'Run SIMD-optimized similarity search, HNSW indexing, and hybrid retrieval in a stack built for real workloads instead of demo traffic.',
    icon: 'lightning',
  },
  {
    title: 'Local Embeddings Without API Dependency',
    description:
      'Generate text and image embeddings with ONNX Runtime locally to reduce latency, cut vendor exposure, and keep sensitive data under your control.',
    icon: 'layers',
    delayClass: 'reveal-delay-1',
  },
  {
    title: 'Multimodal and RAG-Ready from Day One',
    description:
      'Support retrieval pipelines across text, image, document, and structured data workflows with BM25 fusion, chunking strategies, and framework adapters.',
    icon: 'screen',
    delayClass: 'reveal-delay-2',
  },
]

const depthGroups: DepthGroup[] = [
  {
    heading: 'Core Platform',
    items: [
      'SIMD-optimized cosine and Euclidean search',
      'HNSW indexing for low-latency nearest-neighbor retrieval',
      'Memory-mapped storage for efficient large-scale workloads',
      'Rich metadata filtering and universal ingestion adapters',
    ],
  },
  {
    heading: 'AI Workflow Coverage',
    items: [
      'Hybrid search with BM25 and five fusion methods',
      'RAG engine with fixed, sentence, paragraph, semantic, and recursive chunking',
      'Local ONNX text and image embeddings',
      'Python bindings plus CLI and API integration surfaces',
    ],
  },
  {
    heading: 'Operational Readiness',
    items: [
      'Docker, Kubernetes, and bare-metal deployment options',
      'OpenTelemetry tracing, Prometheus metrics, and eBPF observability',
      'Distributed architecture with replication and sharding modes',
      'Support for migrations, architecture review, and incident response',
    ],
  },
]

const segments: Segment[] = [
  {
    number: '01',
    title: 'Performance-Critical Applications',
    description: 'Real-time search, recommendation engines, and latency-sensitive AI systems.',
    lead: 'Lead with latency, throughput, and SIMD optimization.',
  },
  {
    number: '02',
    title: 'Cost-Conscious Enterprises',
    description: 'Teams that want self-hosted vector infrastructure without enterprise license pressure.',
    lead: 'Lead with TCO savings, open-source core, and predictable support tiers.',
    delayClass: 'reveal-delay-1',
  },
  {
    number: '03',
    title: 'Innovation Leaders',
    description: 'Research and multimodal teams exploring image, video, and perceptual quantization workflows.',
    lead: 'Lead with perceptual quantization, ONNX embeddings, and future cloud paths.',
    delayClass: 'reveal-delay-2',
  },
]

const deployments: Deployment[] = [
  {
    title: 'Self-Hosted Core',
    subtitle: 'Full infrastructure control · MIT license',
    description: 'For engineering teams that want no vendor lock-in.',
    pills: ['Community', 'Basic', 'Standard'],
    note: 'Best paired with Community, Basic, or Standard memberships.',
  },
  {
    title: 'Hybrid Enterprise',
    subtitle: 'On-prem + cloud · Regulated environments',
    description:
      'For teams mixing on-prem and cloud environments with sovereignty, compliance, or incident SLA requirements.',
    pills: ['Enterprise Support'],
    note: 'Use Enterprise Support when sovereignty, compliance, or incident SLAs are part of the motion.',
    delayClass: 'reveal-delay-1',
  },
  {
    title: 'Future Managed Cloud',
    subtitle: 'Managed · No-ops delivery',
    description: 'For accounts that prefer a managed option once HEKTOR Cloud launches.',
    pills: ['Planned', '1M vectors', '10M vectors', '100M vectors'],
    note: 'Capture demand now with projected cloud pricing.',
    delayClass: 'reveal-delay-2',
  },
]

const useCases: UseCase[] = [
  {
    number: '01',
    title: 'Production RAG Infrastructure',
    description: 'Route toward Standard or Premium support with migration assistance for enterprise-scale retrieval systems.',
    lead: '→ Standard / Premium Support',
  },
  {
    number: '02',
    title: 'Image and Multimodal Retrieval',
    description: 'Lead with perceptual quantization and billion-scale visual benchmark stories for visual AI systems.',
    lead: '→ Perceptual Quantization · ONNX',
    delayClass: 'reveal-delay-1',
  },
  {
    number: '03',
    title: 'Cost Optimization Migration',
    description: 'Anchor on 60–80% TCO reduction against managed vector databases for self-hosted operators.',
    lead: '→ 60–80% TCO Savings',
    delayClass: 'reveal-delay-2',
  },
  {
    number: '04',
    title: 'Internal Enterprise Search',
    description: 'Position self-hosted deployment and operational support memberships for internal knowledge systems.',
    lead: '→ Self-Hosted + Support Memberships',
    delayClass: 'reveal-delay-3',
  },
]

const pricingCards: PricingCard[] = [
  {
    label: 'Developer-First',
    name: 'Community',
    freeText: 'Free · MIT License',
    features: [
      'Open-source core, no per-vector pricing',
      'No feature gates',
      'Unlimited scale for self-hosted teams',
      'Community documentation and guides',
      'No monthly platform fees',
    ],
    buttonLabel: 'Get Started',
    buttonVariant: 'outline',
  },
  {
    label: 'Evaluation to Launch',
    name: 'Basic Support',
    pricePrefix: '$',
    price: '5,000',
    priceSuffix: '/yr',
    features: ['Email support', '48-hour SLA', 'Quarterly updates', 'Community access'],
    buttonLabel: 'Get Started',
    buttonVariant: 'outline',
  },
  {
    label: 'Production Teams',
    name: 'Standard Support',
    pricePrefix: '$',
    price: '15,000',
    priceSuffix: '/yr',
    features: ['Email and chat support', '24-hour SLA', 'Monthly check-ins', 'Migration assistance'],
    buttonLabel: 'Get Started',
    buttonVariant: 'solid',
    featured: true,
  },
  {
    label: 'Mission-Critical Operations',
    name: 'Premium Support',
    pricePrefix: '$',
    price: '50,000',
    priceSuffix: '/yr',
    features: ['24/7 support', '4-hour SLA', 'Dedicated support engineer', 'Architecture review and training'],
    buttonLabel: 'Get Started',
    buttonVariant: 'outline',
  },
  {
    label: 'Strategic Accounts',
    name: 'Enterprise Support',
    freeText: 'Custom annual contract',
    features: [
      'Named technical account manager',
      '1-hour critical issue SLA',
      'Custom SLA terms',
      'Strategic roadmap influence',
    ],
    buttonLabel: 'Contact Sales',
    buttonVariant: 'outline',
  },
  {
    label: 'Cloud · 1M Vectors',
    name: '',
    plannedBadge: 'Planned · Hektor Cloud',
    pricePrefix: '$',
    price: '400',
    priceSuffix: '/mo',
    features: ['Projected managed-service tier', 'HEKTOR without infrastructure ownership'],
    buttonLabel: 'Request Access',
    buttonVariant: 'outline',
    planned: true,
  },
  {
    label: 'Cloud · 10M Vectors',
    name: '',
    plannedBadge: 'Planned · Hektor Cloud',
    pricePrefix: '$',
    price: '2,000',
    priceSuffix: '/mo',
    features: ['Projected mid-scale route', 'Positioned 40–50% below comparable managed alternatives'],
    buttonLabel: 'Request Access',
    buttonVariant: 'outline',
    planned: true,
  },
  {
    label: 'Cloud · 100M Vectors',
    name: '',
    plannedBadge: 'Planned · Hektor Cloud',
    pricePrefix: '$',
    price: '12,000',
    priceSuffix: '/mo',
    features: ['Projected large-scale managed motion', 'Performance and no-ops delivery'],
    buttonLabel: 'Request Access',
    buttonVariant: 'outline',
    planned: true,
  },
]

const useCaseOptions = [
  { value: '', label: 'Select a use case...' },
  { value: 'rag', label: 'Production RAG Infrastructure' },
  { value: 'search', label: 'Real-Time Search & Recommendation' },
  { value: 'multimodal', label: 'Image & Multimodal Retrieval' },
  { value: 'cost-optimization', label: 'Cost Optimization Migration' },
  { value: 'enterprise-search', label: 'Internal Enterprise Search' },
  { value: 'research', label: 'Research & Innovation' },
  { value: 'other', label: 'Other' },
]

const deploymentOptions = [
  { value: '', label: 'Select deployment path...' },
  { value: 'self-hosted', label: 'Self-Hosted Core' },
  { value: 'hybrid', label: 'Hybrid Enterprise' },
  { value: 'cloud', label: 'Future Managed Cloud' },
  { value: 'undecided', label: 'Undecided' },
]

const scaleOptions = [
  { value: '', label: 'Select scale...' },
  { value: '1m', label: '1M vectors (single-node entry)' },
  { value: '10m', label: '10M vectors (distributed)' },
  { value: '100m', label: '100M+ vectors (large-scale)' },
  { value: '1b', label: '1B+ vectors (billion-scale)' },
]

const initialFormData: FormData = {
  name: '',
  email: '',
  company: '',
  useCase: '',
  deployment: '',
  scale: '',
  message: '',
}

function CapabilityIcon({ icon }: { icon: Capability['icon'] }) {
  if (icon === 'lightning') {
    return (
      <svg viewBox="0 0 24 24" aria-hidden="true">
        <polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2" />
      </svg>
    )
  }

  if (icon === 'layers') {
    return (
      <svg viewBox="0 0 24 24" aria-hidden="true">
        <path d="M12 2L2 7l10 5 10-5-10-5z" />
        <path d="M2 17l10 5 10-5" />
        <path d="M2 12l10 5 10-5" />
      </svg>
    )
  }

  return (
    <svg viewBox="0 0 24 24" aria-hidden="true">
      <rect x="2" y="3" width="20" height="14" rx="2" />
      <path d="M8 21h8" />
      <path d="M12 17v4" />
    </svg>
  )
}

function App() {
  const [mobileMenuOpen, setMobileMenuOpen] = useState(false)
  const [toastMessage, setToastMessage] = useState('')
  const [formData, setFormData] = useState<FormData>(initialFormData)
  const [errors, setErrors] = useState({ name: false, email: false })

  useEffect(() => {
    const revealObserver = new IntersectionObserver(
      (entries) => {
        entries.forEach((entry) => {
          if (entry.isIntersecting) {
            entry.target.classList.add('visible')
          }
        })
      },
      { threshold: 0.1, rootMargin: '0px 0px -50px 0px' },
    )

    const revealed = document.querySelectorAll('.reveal, .hero-metric')
    revealed.forEach((element) => revealObserver.observe(element))

    const showHeroMetrics = window.setTimeout(() => {
      document.querySelectorAll('.hero-metric').forEach((element) => element.classList.add('visible'))
    }, 300)

    const handleScroll = () => {
      const nav = document.querySelector<HTMLElement>('.nav')
      if (!nav) return
      nav.style.borderBottomColor = window.pageYOffset > 100 ? 'rgba(36,48,68,0.8)' : 'var(--border)'
    }

    const handleMouseMove = (event: MouseEvent) => {
      const x = event.clientX / window.innerWidth
      const y = event.clientY / window.innerHeight
      const glow1 = document.querySelector<HTMLElement>('.ambient-glow-1')
      const glow2 = document.querySelector<HTMLElement>('.ambient-glow-2')
      if (glow1) {
        glow1.style.transform = `translate(${x * 30}px, ${y * 30}px)`
      }
      if (glow2) {
        glow2.style.transform = `translate(${-x * 20}px, ${-y * 20}px)`
      }
    }

    window.addEventListener('scroll', handleScroll)
    window.addEventListener('mousemove', handleMouseMove)
    handleScroll()

    return () => {
      window.clearTimeout(showHeroMetrics)
      revealObserver.disconnect()
      window.removeEventListener('scroll', handleScroll)
      window.removeEventListener('mousemove', handleMouseMove)
    }
  }, [])

  useEffect(() => {
    if (!toastMessage) return undefined

    const timeoutId = window.setTimeout(() => setToastMessage(''), 4000)
    return () => window.clearTimeout(timeoutId)
  }, [toastMessage])

  const optionLabels = useMemo(
    () => ({
      useCase: new Map(useCaseOptions.map((option) => [option.value, option.label])),
      deployment: new Map(deploymentOptions.map((option) => [option.value, option.label])),
      scale: new Map(scaleOptions.map((option) => [option.value, option.label])),
    }),
    [],
  )

  const closeMobileMenu = () => setMobileMenuOpen(false)

  const handleInputChange = (
    event: ChangeEvent<HTMLInputElement | HTMLTextAreaElement | HTMLSelectElement>,
  ) => {
    const { name, value } = event.target
    setFormData((current) => ({ ...current, [name]: value }))

    if (name === 'name' || name === 'email') {
      setErrors((current) => ({
        ...current,
        [name]: false,
      }))
    }
  }

  const handleSignup = (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault()

    const nextErrors = {
      name: !formData.name.trim(),
      email: !formData.email.trim() || !formData.email.includes('@'),
    }
    setErrors(nextErrors)

    if (nextErrors.name || nextErrors.email) {
      return
    }

    const subject = 'HEKTOR Platform — Signup Request'
    const body = encodeURIComponent(`HEKTOR Platform Signup Request
===========================

Name: ${formData.name.trim()}
Email: ${formData.email.trim()}
Company: ${formData.company.trim() || 'Not specified'}

Use Case: ${optionLabels.useCase.get(formData.useCase) ?? useCaseOptions[0].label}
Deployment Target: ${optionLabels.deployment.get(formData.deployment) ?? deploymentOptions[0].label}
Expected Scale: ${optionLabels.scale.get(formData.scale) ?? scaleOptions[0].label}

Additional Details:
${formData.message.trim() || 'No additional details provided.'}

---
Generated from HEKTOR Platform pricing page`)

    window.open(`mailto:support@artifactvirtual.com?subject=${encodeURIComponent(subject)}&body=${body}`)
    setToastMessage('Mail client opened with your signup request ready to send.')
  }

  return (
    <div className="app-shell">
      <div className="stone-overlay" />
      <div className="light-rays" />
      <div className="ambient-glow ambient-glow-1" />
      <div className="ambient-glow ambient-glow-2" />

      <nav className="nav" role="navigation" aria-label="Main navigation">
        <a href="#hero" className="nav-logo" onClick={closeMobileMenu}>
          HEKTOR<span>.</span>
        </a>
        <ul className="nav-links">
          <li>
            <a href="#performance">Performance</a>
          </li>
          <li>
            <a href="#platform">Platform</a>
          </li>
          <li>
            <a href="#pricing">Pricing</a>
          </li>
          <li>
            <a href="#signup">Signup</a>
          </li>
        </ul>
        <a href="#signup" className="nav-cta" onClick={closeMobileMenu}>
          Get Started
        </a>
        <button
          type="button"
          className="nav-mobile-btn"
          aria-label="Toggle menu"
          aria-expanded={mobileMenuOpen}
          onClick={() => setMobileMenuOpen((current) => !current)}
        >
          ☰
        </button>
      </nav>

      <div className={`mobile-menu ${mobileMenuOpen ? 'active' : ''}`} id="mobileMenu">
        <a href="#performance" onClick={closeMobileMenu}>
          Performance
        </a>
        <a href="#platform" onClick={closeMobileMenu}>
          Platform
        </a>
        <a href="#use-cases" onClick={closeMobileMenu}>
          Use Cases
        </a>
        <a href="#pricing" onClick={closeMobileMenu}>
          Pricing
        </a>
        <a href="#signup" onClick={closeMobileMenu}>
          Signup
        </a>
      </div>

      <main>
        <section className="hero" id="hero">
          <div className="hero-badge">Production Vector Infrastructure</div>
          <h1>
            Vector Search Built for
            <br />
            <span className="accent">Real AI Workloads</span>
          </h1>
          <p className="subtitle">
            Build faster retrieval systems with open-source infrastructure you can control. HEKTOR
            delivers SIMD-optimized vector search, hybrid retrieval, and local embeddings — with a
            practical path from single-node deployments to billion-scale architectures.
          </p>
          <div className="hero-actions">
            <a href="#pricing" className="btn-primary">
              Explore Pricing
            </a>
            <a href="#signup" className="btn-outline">
              Signup Request
            </a>
          </div>
          <div className="hero-metrics">
            {heroMetrics.map((metric) => {
              return (
                <div className="hero-metric" key={`${metric.value}-${metric.tail ?? ''}`}>
                  <div className="metric-value">
                    {metric.value}
                    {metric.unit ? <span className="unit">{metric.unit}</span> : null}
                    {metric.tail ? ` ${metric.tail}` : null}
                  </div>
                  <div className="metric-label">{metric.label}</div>
                </div>
              )
            })}
          </div>
        </section>

        <section className="performance" id="performance">
          <div className="section-header reveal">
            <div className="section-tag">Performance Leadership</div>
            <h2 className="section-title">2.9ms p99 Position</h2>
            <p className="section-desc">
              HEKTOR is the low-latency route for production retrieval and recommendation workloads.
            </p>
          </div>
          <div className="performance-grid">
            {performanceCards.map((card, index) => (
              <div className={`perf-card reveal${index > 0 ? ` reveal-delay-${index}` : ''}`} key={card.number}>
                <div className="perf-number">
                  {card.number.replace(card.highlight, '')}
                  <span className="highlight">{card.highlight}</span>
                </div>
                <div className="perf-desc">{card.description}</div>
                <div className="perf-source">{card.source}</div>
              </div>
            ))}
          </div>
        </section>

        <section className="capabilities" id="platform">
          <div className="section-header reveal">
            <div className="section-tag">Why Teams Buy Hektor</div>
            <h2 className="section-title">A Commercial Story Grounded in the Product</h2>
            <p className="section-desc">Not inflated marketing copy. Just production capabilities that matter.</p>
          </div>
          <div className="cap-grid">
            {capabilities.map((capability) => (
              <div className={`cap-card reveal ${capability.delayClass ?? ''}`.trim()} key={capability.title}>
                <div className="cap-icon">
                  <CapabilityIcon icon={capability.icon} />
                </div>
                <h3>{capability.title}</h3>
                <p>{capability.description}</p>
              </div>
            ))}
          </div>
        </section>

        <section className="platform-depth" id="depth">
          <div className="section-header reveal">
            <div className="section-tag">Platform Depth</div>
            <h2 className="section-title">The Feature Surface Expected from Serious Vector Infrastructure</h2>
          </div>
          <div className="depth-grid reveal">
            {depthGroups.map((group) => (
              <div className="depth-group" key={group.heading}>
                <h4>{group.heading}</h4>
                {group.items.map((item) => (
                  <div className="depth-item" key={item}>
                    <span className="check" />
                    {item}
                  </div>
                ))}
              </div>
            ))}
          </div>
        </section>

        <section className="segments">
          <div className="section-header reveal">
            <div className="section-tag">Target Segments</div>
            <h2 className="section-title">Where HEKTOR Wins</h2>
            <p className="section-desc">Three buyer profiles, three distinct value positions.</p>
          </div>
          <div className="segments-grid">
            {segments.map((segment) => (
              <div className={`seg-card reveal ${segment.delayClass ?? ''}`.trim()} key={segment.number}>
                <div className="seg-icon">{segment.number}</div>
                <h3>{segment.title}</h3>
                <p>{segment.description}</p>
                <div className="seg-lead">{segment.lead}</div>
              </div>
            ))}
          </div>
        </section>

        <section className="deployment">
          <div className="section-header reveal">
            <div className="section-tag">Deployment Options</div>
            <h2 className="section-title">Adopt HEKTOR on Your Terms</h2>
          </div>
          <div className="deployment-grid">
            {deployments.map((deployment) => (
              <div className={`dep-card reveal ${deployment.delayClass ?? ''}`.trim()} key={deployment.title}>
                <h3>{deployment.title}</h3>
                <div className="dep-subtitle">{deployment.subtitle}</div>
                <p>{deployment.description}</p>
                {deployment.pills.map((pill) => (
                  <span className="dep-pill" key={pill}>
                    {pill}
                  </span>
                ))}
                <div className="dep-note">{deployment.note}</div>
              </div>
            ))}
          </div>
        </section>

        <section className="use-cases" id="use-cases">
          <div className="section-header reveal">
            <div className="section-tag">Operational Use Cases</div>
            <h2 className="section-title">Deployment Priorities Aligned to Buyer Intent</h2>
          </div>
          <div className="cases-grid">
            {useCases.map((useCase) => (
              <div className={`case-item reveal ${useCase.delayClass ?? ''}`.trim()} key={useCase.number}>
                <div className="case-num">{useCase.number}</div>
                <div className="case-content">
                  <h4>{useCase.title}</h4>
                  <p>{useCase.description}</p>
                  <div className="case-lead">{useCase.lead}</div>
                </div>
              </div>
            ))}
          </div>
        </section>

        <section className="pricing-section" id="pricing">
          <div className="section-header reveal">
            <div className="section-tag">Pricing Structure</div>
            <h2 className="section-title">Open-Source Core · Optional Support · Clear Path to Managed Service</h2>
            <p className="section-desc">
              The core product is free under MIT. Commercial spend starts only when a team wants
              response SLAs, migration support, architecture review, or strategic partnership.
            </p>
          </div>
          <div className="pricing-grid">
            {pricingCards.map((card) => (
              <div
                className={`pricing-card reveal ${card.featured ? 'featured' : ''} ${card.planned ? 'planned' : ''}`.trim()}
                key={`${card.label}-${card.price ?? card.freeText}`}
              >
                {card.plannedBadge ? <div className="pc-planned-badge">{card.plannedBadge}</div> : null}
                <div className="pc-label">{card.label}</div>
                {card.name ? <div className="pc-name">{card.name}</div> : null}
                {card.price ? (
                  <div className="pc-price">
                    {card.pricePrefix ? <span className="pc-currency">{card.pricePrefix}</span> : null}
                    {card.price}
                    {card.priceSuffix ? <span className="pc-period">{card.priceSuffix}</span> : null}
                  </div>
                ) : null}
                {card.freeText ? <div className="pc-free">{card.freeText}</div> : null}
                <ul className="pc-features">
                  {card.features.map((feature) => (
                    <li key={feature}>{feature}</li>
                  ))}
                </ul>
                <a href="#signup" className={`pc-btn ${card.buttonVariant}`}>
                  {card.buttonLabel}
                </a>
              </div>
            ))}
          </div>
        </section>

        <section className="signup" id="signup">
          <div className="section-header reveal">
            <div className="section-tag">Signup</div>
            <h2 className="section-title">Request Access and Support for Your HEKTOR Deployment</h2>
            <p className="section-desc">
              Send your request to <strong className="accent-text">support@artifactvirtual.com</strong> and
              include your use case, deployment target, and expected scale.
            </p>
          </div>
          <div className="signup-container">
            <div className="signup-info reveal">
              <h3>Compose Your Signup Request</h3>
              <p>
                Use one click to compose your signup request and send it to support@artifactvirtual.com.
                Include your use case, deployment target, and expected scale — and we&apos;ll respond with
                a tailored architecture recommendation.
              </p>
              <a href="mailto:support@artifactvirtual.com" className="signup-email-link">
                Or email directly
                <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" aria-hidden="true">
                  <path d="M5 12h14M12 5l7 7-7 7" />
                </svg>
              </a>
              <p className="signup-note signup-note-spaced">
                <strong>What to include:</strong> Your use case (RAG, search, recommendation),
                deployment target (self-hosted, hybrid, cloud), and expected scale (1M, 10M,
                100M+ vectors). We&apos;ll match you to the right membership tier or managed-service path.
              </p>
            </div>

            <form className="signup-form reveal reveal-delay-1" id="signupForm" onSubmit={handleSignup}>
              <div className="form-row">
                <div className="form-group">
                  <label htmlFor="fName">Full Name</label>
                  <input
                    type="text"
                    id="fName"
                    name="name"
                    required
                    placeholder="Jane Doe"
                    value={formData.name}
                    onChange={handleInputChange}
                  />
                  <div className="form-error" style={{ display: errors.name ? 'block' : 'none' }}>
                    Name is required
                  </div>
                </div>
                <div className="form-group">
                  <label htmlFor="fEmail">Email</label>
                  <input
                    type="email"
                    id="fEmail"
                    name="email"
                    required
                    placeholder="jane@company.com"
                    value={formData.email}
                    onChange={handleInputChange}
                  />
                  <div className="form-error" style={{ display: errors.email ? 'block' : 'none' }}>
                    Valid email is required
                  </div>
                </div>
              </div>
              <div className="form-group">
                <label htmlFor="fCompany">Company</label>
                <input
                  type="text"
                  id="fCompany"
                  name="company"
                  placeholder="Acme Corp"
                  value={formData.company}
                  onChange={handleInputChange}
                />
              </div>
              <div className="form-group">
                <label htmlFor="fUseCase">Use Case</label>
                <select id="fUseCase" name="useCase" value={formData.useCase} onChange={handleInputChange}>
                  {useCaseOptions.map((option) => (
                    <option key={option.label} value={option.value}>
                      {option.label}
                    </option>
                  ))}
                </select>
              </div>
              <div className="form-group">
                <label htmlFor="fDeployment">Deployment Target</label>
                <select
                  id="fDeployment"
                  name="deployment"
                  value={formData.deployment}
                  onChange={handleInputChange}
                >
                  {deploymentOptions.map((option) => (
                    <option key={option.label} value={option.value}>
                      {option.label}
                    </option>
                  ))}
                </select>
              </div>
              <div className="form-group">
                <label htmlFor="fScale">Expected Scale</label>
                <select id="fScale" name="scale" value={formData.scale} onChange={handleInputChange}>
                  {scaleOptions.map((option) => (
                    <option key={option.label} value={option.value}>
                      {option.label}
                    </option>
                  ))}
                </select>
              </div>
              <div className="form-group">
                <label htmlFor="fMessage">Additional Details</label>
                <textarea
                  id="fMessage"
                  name="message"
                  placeholder="Describe your retrieval workload, latency requirements, or migration needs..."
                  value={formData.message}
                  onChange={handleInputChange}
                />
              </div>
              <button type="submit" className="btn-submit">
                Compose Signup Request
              </button>
            </form>
          </div>
        </section>
      </main>

      <footer className="footer">
        <div className="footer-brand">
          HEKTOR<span>.</span>
        </div>
        <ul className="footer-links">
          <li>
            <a href="#performance">Performance</a>
          </li>
          <li>
            <a href="#platform">Platform</a>
          </li>
          <li>
            <a href="#pricing">Pricing</a>
          </li>
          <li>
            <a href="#signup">Signup</a>
          </li>
        </ul>
        <div className="footer-copy">© 2025 HEKTOR Platform · support@artifactvirtual.com</div>
      </footer>

      <div className={`toast ${toastMessage ? 'show' : ''}`} id="toast">
        {toastMessage}
      </div>
    </div>
  )
}

export default App
