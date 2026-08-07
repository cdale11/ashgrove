import { EMOTION_NAMES } from '../types'
import type { NPC, WorldState } from '../types'

interface NPCDetailPanelProps {
  state: WorldState
  npc: NPC
}

export function NPCDetailPanel({ state, npc }: NPCDetailPanelProps) {
  const npcById = (id: number): NPC | undefined =>
    state.world.npcs.find((n) => n.id === id)

  return (
    <div className="panel npc-detail-panel">
      <h3>{npc.name} {npc.surname}</h3>
      <div className="npc-meta">
        <span>{npc.age} years old · {npc.gender}</span>
        <span className="occupation">{npc.occupation}</span>
        <span>Health {Math.round(npc.health)}%</span>
      </div>

      <div className="npc-vitals">
        <div className="vital">
          <span className="vital-label">Hunger</span>
          <div className="bar"><div className="bar-fill" style={{ width: `${npc.hunger}%`, background: npc.hunger > 60 ? '#c0392b' : '#e67e22' }} /></div>
        </div>
        <div className="vital">
          <span className="vital-label">Fatigue</span>
          <div className="bar"><div className="bar-fill" style={{ width: `${npc.fatigue}%`, background: '#8e44ad' }} /></div>
        </div>
        <div className="vital">
          <span className="vital-label">Reputation</span>
          <div className="bar"><div className="bar-fill" style={{ width: `${Math.min(100, Math.max(0, (npc.reputation + 100) / 2))}%`, background: npc.reputation >= 0 ? '#27ae60' : '#c0392b' }} /></div>
        </div>
      </div>

      <div className="npc-emotion">
        <span className="emotion-label">Current mood:</span>
        <span className={`emotion-text emotion-${npc.current_emotion}`}>
          {EMOTION_NAMES[npc.current_emotion] ?? 'Unknown'}
          {npc.emotion_intensity > 0.2 ? ` (${Math.round(npc.emotion_intensity * 100)}%)` : ''}
        </span>
      </div>

      <div className="detail-section">
        <h4>Personality</h4>
        <div className="personality-list">
          {npc.personality.map((trait) => (
            <div key={trait.name} className="trait-row">
              <span className="trait-name">{trait.name}</span>
              <div className="bar small"><div className="bar-fill" style={{ width: `${trait.value * 100}%`, background: '#3498db' }} /></div>
              <span className="trait-value">{Math.round(trait.value * 100)}</span>
            </div>
          ))}
        </div>
      </div>

      <div className="detail-section">
        <h4>Beliefs ({npc.beliefs.length})</h4>
        <ul className="belief-list">
          {npc.beliefs.map((belief, i) => (
            <li key={i} className={`belief ${belief.is_core ? 'core' : ''}`}>
              <span className="belief-text">{belief.proposition}</span>
              <span className="belief-confidence" style={{ opacity: 0.4 + belief.confidence * 0.6 }}>
                {Math.round(belief.confidence * 100)}%
              </span>
            </li>
          ))}
          {npc.beliefs.length === 0 && <li className="empty">No beliefs recorded</li>}
        </ul>
      </div>

      <div className="detail-section">
        <h4>Goals ({npc.goals.length})</h4>
        <ul className="goal-list">
          {npc.goals.map((goal, i) => (
            <li key={i} className={`goal goal-${goal.status}`}>
              <span className="goal-text">{goal.description}</span>
              <span className="goal-progress">{Math.round(goal.progress * 100)}%</span>
            </li>
          ))}
          {npc.goals.length === 0 && <li className="empty">No active goals</li>}
        </ul>
      </div>

      {npc.relationships.length > 0 && (
        <div className="detail-section">
          <h4>Relationships</h4>
          <ul className="relationship-list">
            {npc.relationships.map((rel, i) => {
              const target = npcById(rel.target_id)
              return (
                <li key={i} className="relationship">
                  <span className="rel-target">{target ? `${target.name} ${target.surname}` : `#${rel.target_id}`}</span>
                  <span className={`rel-type ${rel.type}`}>{rel.type}</span>
                  <span className="rel-affinity" style={{ color: rel.affinity >= 0 ? '#27ae60' : '#c0392b' }}>
                    {Math.round(rel.affinity * 100)}
                  </span>
                  <span className="rel-trust">T:{Math.round(rel.trust * 100)}</span>
                </li>
              )
            })}
          </ul>
        </div>
      )}

      <div className="detail-section">
        <h4>Recent Memories ({npc.memories.length})</h4>
        <ul className="memory-list">
          {npc.memories.slice(-5).reverse().map((mem, i) => (
            <li key={i} className={`memory ${mem.is_false ? 'false-memory' : ''}`}>
              <span className="memory-text">{mem.description}</span>
              {mem.is_false && <span className="memory-flag">⚠ MISREMEMBERED</span>}
            </li>
          ))}
          {npc.memories.length === 0 && <li className="empty">No memories recorded</li>}
        </ul>
      </div>
    </div>
  )
}