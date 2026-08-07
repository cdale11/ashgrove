import { KNOWLEDGE_CATEGORY_NAMES } from '../types'
import type { WorldState } from '../types'

interface InvestigationPanelProps {
  state: WorldState
}

const CATEGORY_COLORS = ['#8e44ad', '#2980b9', '#27ae60', '#f39c12', '#c0392b', '#2c3e50', '#16a085', '#7f8c8d', '#95a5a6']

export function InvestigationPanel({ state }: InvestigationPanelProps) {
  const { knowledge, evidence } = state.investigation
  const contradictions = evidence.filter((e) => e.is_contradictory)

  return (
    <div className="panel investigation-panel">
      <h3>Investigation</h3>

      <div className="detail-section">
        <h4>Knowledge ({knowledge.length})</h4>
        <ul className="knowledge-list">
          {knowledge.map((k) => (
            <li key={k.id} className="knowledge-entry">
              <span
                className="knowledge-category"
                style={{ background: CATEGORY_COLORS[k.category] ?? '#95a5a6' }}
              >
                {KNOWLEDGE_CATEGORY_NAMES[k.category] ?? 'Unknown'}
              </span>
              <span className="knowledge-title">{k.title}</span>
              <span className="knowledge-completeness">
                {Math.round(k.completeness * 100)}%
              </span>
            </li>
          ))}
          {knowledge.length === 0 && <li className="empty">Nothing known yet</li>}
        </ul>
      </div>

      <div className="detail-section">
        <h4>Evidence ({evidence.length})</h4>
        <ul className="evidence-list">
          {evidence.map((e) => (
            <li key={e.id} className={`evidence-entry ${e.is_contradictory ? 'contradictory' : ''}`}>
              <span className="evidence-name">{e.name}</span>
              <span className="evidence-reliability">
                {Math.round(e.reliability * 100)}% reliable
              </span>
              <span className="evidence-tags">
                {e.tags.map((tag) => (
                  <span key={tag} className="tag">{tag}</span>
                ))}
              </span>
            </li>
          ))}
          {evidence.length === 0 && <li className="empty">No evidence collected</li>}
        </ul>
      </div>

      {contradictions.length > 0 && (
        <div className="detail-section contradiction-warning">
          <h4>⚠ Contradictions Detected</h4>
          <p>Some evidence conflicts with other records. Re-examine your assumptions.</p>
        </div>
      )}
    </div>
  )
}