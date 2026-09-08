import { FormEvent, useEffect, useMemo, useState } from 'react'
import themeData from './data/theme.json'
import pricingData from './data/pricing.json'
import modelingData from './data/modeling.json'
import membershipData from './data/memberships.json'

type MembershipStage = 'New Signup' | 'Qualified' | 'Active' | 'Renewal'

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

interface MembershipRecord {
  id: string
  organization: string
  contact: string
  planId: string
  stage: MembershipStage
  useCase: string
  vectorScale: string
  owner: string
  nextStep: string
  priority: string
}

interface SignupFormState {
  organization: string
  contact: string
  planId: string
  useCase: string
  vectorScale: string
  notes: string
}

const theme = themeData as ThemeData
const pricing = pricingData as PricingData
const modeling = modelingData as ModelingData
const seededMemberships = membershipData.records as MembershipRecord[]
const stages: Array<MembershipStage | 'All'> = ['All', 'New Signup', 'Qualified', 'Active', 'Renewal']
const stageOrder: MembershipStage[] = ['New Signup', 'Qualified', 'Active', 'Renewal']

const createInitialForm = (): SignupFormState => ({
  organization: '',
  contact: '',
  planId: pricing.plans[2]?.id ?? pricing.plans[0].id,
  useCase: modeling.useCases[0]?.label ?? 'Production search',
  vectorScale: '10M',
  notes: '',
})

function App() {
  const [memberships, setMemberships] = useState<MembershipRecord[]>(seededMemberships)
  const [stageFilter, setStageFilter] = useState<(typeof stages)[number]>('All')
  const [formState, setFormState] = useState<SignupFormState>(createInitialForm)

  const plansById = useMemo(
    () => new Map(pricing.plans.map((plan) => [plan.id, plan])),
    [],
  )

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

  const summary = useMemo(() => {
    const activeCount = memberships.filter((membership) => membership.stage === 'Active').length
    const qualifiedCount = memberships.filter((membership) => membership.stage === 'Qualified').length
    const trackedArr = memberships.reduce((total, membership) => {
      if (membership.stage !== 'Active' && membership.stage !== 'Renewal') {
        return total
      }

      return total + (plansById.get(membership.planId)?.annualPriceUsd ?? 0)
    }, 0)

    return {
      total: memberships.length,
      activeCount,
      qualifiedCount,
      trackedArr,
    }
  }, [memberships, plansById])

  const filteredMemberships = useMemo(() => {
    if (stageFilter === 'All') {
      return memberships
    }

    return memberships.filter((membership) => membership.stage === stageFilter)
  }, [memberships, stageFilter])

  const advanceStage = (recordId: string) => {
    setMemberships((current) =>
      current.map((record) => {
        if (record.id !== recordId) {
          return record
        }

        const currentIndex = stageOrder.indexOf(record.stage)
        const nextStage = stageOrder[Math.min(currentIndex + 1, stageOrder.length - 1)]

        return {
          ...record,
          stage: nextStage,
          nextStep:
            nextStage === 'Qualified'
              ? 'Schedule sizing call'
              : nextStage === 'Active'
                ? 'Onboard deployment'
                : 'Confirm renewal timing',
        }
      }),
    )
  }

  const handleSubmit = (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault()

    const selectedPlan = plansById.get(formState.planId)
    const newRecord: MembershipRecord = {
      id: `SG-${String(memberships.length + 1).padStart(3, '0')}`,
      organization: formState.organization.trim(),
      contact: formState.contact.trim(),
      planId: formState.planId,
      stage: 'New Signup',
      useCase: formState.useCase,
      vectorScale: formState.vectorScale,
      owner: 'Gateway queue',
      nextStep: selectedPlan?.name === 'Community' ? 'Send self-hosted onboarding' : 'Review membership fit',
      priority: formState.vectorScale === '1B+' ? 'Critical' : formState.vectorScale === '100M' ? 'High' : 'Standard',
    }

    setMemberships((current) => [newRecord, ...current])
    setStageFilter('All')
    setFormState(createInitialForm())
  }

  return (
    <div className="app-shell">
      <div className="grain-layer" />
      <header className="topbar">
        <div>
          <p className="eyebrow">Kinetic membership gateway</p>
          <h1>HEKTOR</h1>
        </div>
        <div className="topbar-stats">
          <div>
            <span>{summary.total}</span>
            <small>tracked signups</small>
          </div>
          <div>
            <span>{summary.activeCount}</span>
            <small>active memberships</small>
          </div>
          <div>
            <span>${summary.trackedArr.toLocaleString()}</span>
            <small>annualized support</small>
          </div>
        </div>
      </header>

      <main className="layout">
        <section className="hero panel">
          <div className="hero-copy">
            <p className="eyebrow">Ionized neural processing</p>
            <h2>
              Sign up, route, and activate <span>HEKTOR memberships</span>.
            </h2>
            <p className="hero-text">
              Built from the existing ionized haze visual language and grounded in the repository&apos;s
              pricing, support, and positioning data so the root app is ready for Vercel deployment.
            </p>
            <div className="hero-actions">
              <a className="beam-button" href="#signup-form">
                Create signup
              </a>
              <a className="ghost-button" href="#pricing">
                Review memberships
              </a>
            </div>
          </div>

          <div className="signal-board">
            <div className="terminal-card">
              <div className="terminal-head">
                <span />
                <span />
                <span />
              </div>
              <div className="terminal-copy">
                <p>&gt; latency_profile: {pricing.benchmarks.latency}</p>
                <p>&gt; tco_delta: {pricing.benchmarks.tcoSavings}</p>
                <p>&gt; recall_profile: {pricing.benchmarks.recall}</p>
                <p>&gt; routes_loaded: {modeling.deploymentModels.length}</p>
                <p>&gt; membership_plans: {pricing.plans.length}</p>
              </div>
            </div>
          </div>
        </section>

        <section className="stats-grid">
          {modeling.valueProposition.map((signal) => (
            <article className="panel stat-card" key={signal.label}>
              <p className="stat-label">{signal.label}</p>
              <h3>{signal.value}</h3>
              <p>{signal.context}</p>
            </article>
          ))}
        </section>

        <section className="workspace">
          <article className="panel signup-panel" id="signup-form">
            <div className="section-heading">
              <p className="eyebrow">Gateway intake</p>
              <h3>Create a new membership signup</h3>
            </div>

            <form className="signup-form" onSubmit={handleSubmit}>
              <label>
                Organization
                <input
                  required
                  value={formState.organization}
                  onChange={(event) =>
                    setFormState((current) => ({ ...current, organization: event.target.value }))
                  }
                  placeholder="Acme Retrieval"
                />
              </label>

              <label>
                Contact
                <input
                  required
                  type="email"
                  value={formState.contact}
                  onChange={(event) =>
                    setFormState((current) => ({ ...current, contact: event.target.value }))
                  }
                  placeholder="team@company.com"
                />
              </label>

              <label>
                Membership
                <select
                  value={formState.planId}
                  onChange={(event) =>
                    setFormState((current) => ({ ...current, planId: event.target.value }))
                  }
                >
                  {pricing.plans.map((plan) => (
                    <option key={plan.id} value={plan.id}>
                      {plan.name} · {plan.priceLabel}
                    </option>
                  ))}
                </select>
              </label>

              <label>
                Use case
                <select
                  value={formState.useCase}
                  onChange={(event) =>
                    setFormState((current) => ({ ...current, useCase: event.target.value }))
                  }
                >
                  {modeling.useCases.map((useCase) => (
                    <option key={useCase.label} value={useCase.label}>
                      {useCase.label}
                    </option>
                  ))}
                </select>
              </label>

              <label>
                Vector scale
                <select
                  value={formState.vectorScale}
                  onChange={(event) =>
                    setFormState((current) => ({ ...current, vectorScale: event.target.value }))
                  }
                >
                  <option value="1M">1M</option>
                  <option value="10M">10M</option>
                  <option value="100M">100M</option>
                  <option value="1B+">1B+</option>
                </select>
              </label>

              <label>
                Routing notes
                <textarea
                  rows={4}
                  value={formState.notes}
                  onChange={(event) =>
                    setFormState((current) => ({ ...current, notes: event.target.value }))
                  }
                  placeholder="Migration timing, cloud preference, compliance asks..."
                />
              </label>

              <button className="beam-button" type="submit">
                Initialize signup
              </button>
            </form>
          </article>

          <article className="panel queue-panel">
            <div className="section-heading section-heading-inline">
              <div>
                <p className="eyebrow">Membership queue</p>
                <h3>Manage signup progression</h3>
              </div>
              <select value={stageFilter} onChange={(event) => setStageFilter(event.target.value as (typeof stages)[number])}>
                {stages.map((stage) => (
                  <option key={stage} value={stage}>
                    {stage}
                  </option>
                ))}
              </select>
            </div>

            <div className="queue-list">
              {filteredMemberships.map((record) => {
                const plan = plansById.get(record.planId)

                return (
                  <article className="queue-item" key={record.id}>
                    <div className="queue-meta">
                      <div>
                        <p className="queue-id">{record.id}</p>
                        <h4>{record.organization}</h4>
                      </div>
                      <span className={`stage-pill stage-${record.stage.toLowerCase().replace(/\s+/g, '-')}`}>
                        {record.stage}
                      </span>
                    </div>
                    <p className="queue-contact">{record.contact}</p>
                    <dl className="queue-grid">
                      <div>
                        <dt>Membership</dt>
                        <dd>{plan?.name ?? record.planId}</dd>
                      </div>
                      <div>
                        <dt>Use case</dt>
                        <dd>{record.useCase}</dd>
                      </div>
                      <div>
                        <dt>Scale</dt>
                        <dd>{record.vectorScale}</dd>
                      </div>
                      <div>
                        <dt>Priority</dt>
                        <dd>{record.priority}</dd>
                      </div>
                      <div>
                        <dt>Owner</dt>
                        <dd>{record.owner}</dd>
                      </div>
                      <div>
                        <dt>Next step</dt>
                        <dd>{record.nextStep}</dd>
                      </div>
                    </dl>
                    <button className="ghost-button" onClick={() => advanceStage(record.id)} type="button">
                      Advance stage
                    </button>
                  </article>
                )
              })}
            </div>
          </article>
        </section>

        <section className="pricing-layout" id="pricing">
          <div className="section-heading">
            <p className="eyebrow">Membership catalog</p>
            <h3>Pricing pulled from the repository strategy docs</h3>
          </div>

          <div className="pricing-grid">
            {pricing.plans.map((plan) => (
              <article className="panel pricing-card" key={plan.id}>
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
                <p className="eyebrow">Projected cloud motion</p>
                <h4>{projection.name}</h4>
                <strong>{projection.priceLabel}</strong>
                <p>{projection.notes}</p>
              </article>
            ))}
          </div>
        </section>

        <section className="model-grid">
          <article className="panel">
            <div className="section-heading">
              <p className="eyebrow">Market model</p>
              <h3>Primary target segments</h3>
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

          <article className="panel">
            <div className="section-heading">
              <p className="eyebrow">Deployment routes</p>
              <h3>Qualification motions</h3>
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

          <article className="panel">
            <div className="section-heading">
              <p className="eyebrow">Use-case routing</p>
              <h3>HEKTOR solution motions</h3>
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
      </main>
    </div>
  )
}

export default App
