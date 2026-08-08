import { useState } from 'react'
import type { WorldState } from '../types'

interface JournalPanelProps {
  state: WorldState
  onClose: () => void
}

const CATEGORY_LABELS: Record<number, string> = {
  0: 'History',
  1: 'Person',
  2: 'Location',
  3: 'Event',
  4: 'Rumor',
  5: 'Mystery',
  6: 'Secret',
}

const CATEGORY_COLORS: Record<number, string> = {
  0: '#8b7355',
  1: '#a89060',
  2: '#7a8b55',
  3: '#8b5560',
  4: '#a87055',
  5: '#556b8b',
  6: '#6b558b',
}

export function JournalPanel({ state, onClose }: JournalPanelProps) {
  const [tab, setTab] = useState<'knowledge' | 'evidence' | 'log'>('knowledge')
  const investigation = state.investigation
  const player = state.player

  const sortedKnowledge = [...(investigation?.knowledge ?? [])].sort((a, b) => 
    (b.discovered_at ?? 0) - (a.discovered_at ?? 0)
  )

  const getCompletenessColor = (c: number) => {
    if (c >= 0.8) return '#4ade80'
    if (c >= 0.5) return '#fbbf24'
    if (c >= 0.25) return '#f87171'
    return '#9ca3af'
  }

  return (
    <div className="panel journal-panel">
      <div className="journal-header">
        <h3>📔 Investigator's Journal</h3>
        <button className="close-btn" onClick={onClose}>✕</button>
      </div>

      <div className="journal-tabs">
        <button 
          className={tab === 'knowledge' ? 'active' : ''} 
          onClick={() => setTab('knowledge')}
        >
          Knowledge ({investigation?.knowledge?.length ?? 0})
        </button>
        <button 
          className={tab === 'evidence' ? 'active' : ''} 
          onClick={() => setTab('evidence')}
        >
          Evidence ({investigation?.evidence?.length ?? 0})
        </button>
        <button 
          className={tab === 'log' ? 'active' : ''} 
          onClick={() => setTab('log')}
        >
          Log ({player?.action_log?.length ?? 0})
        </button>
      </div>

      <div className="journal-content">
        {tab === 'knowledge' && (
          <div className="knowledge-list">
            {sortedKnowledge.length === 0 ? (
              <p className="empty-state">No knowledge recorded yet. Talk to villagers, inspect items, and explore buildings to uncover secrets.</p>
            ) : (
              sortedKnowledge.map((k) => (
                <div key={k.id} className="knowledge-entry">
                  <div className="knowledge-header">
                    <span className="knowledge-title">{k.title}</span>
                    <span className="knowledge-category" style={{ backgroundColor: CATEGORY_COLORS[k.category] ?? '#555' }}>
                      {CATEGORY_LABELS[k.category] ?? 'Unknown'}
                    </span>
                  </div>
                  <div className="knowledge-completeness">
                    <div className="completeness-bar">
                      <div 
                        className="completeness-fill" 
                        style={{ 
                          width: `${Math.round((k.completeness ?? 0) * 100)}%`,
                          backgroundColor: getCompletenessColor(k.completeness ?? 0)
                        }} 
                      />
                    </div>
                    <span className="completeness-text">{Math.round((k.completeness ?? 0) * 100)}% complete</span>
                  </div>
                  <p className="knowledge-description">{k.description}</p>
                  {(k.source_npc_ids?.length ?? 0) > 0 && (
                    <p className="knowledge-sources">
                      Sources: {k.source_npc_ids.map((id: number) => `NPC #${id}`).join(', ')}
                    </p>
                  )}
                  {k.unlock_requirement && (
                    <p className="knowledge-unlock">Requires: {k.unlock_requirement}</p>
                  )}
                </div>
              ))
            )}
          </div>
        )}

        {tab === 'evidence' && (
          <div className="evidence-list">
            {(!investigation?.evidence || investigation.evidence.length === 0) ? (
              <p className="empty-state">No physical evidence collected. Inspect items and search locations thoroughly.</p>
            ) : (
              investigation.evidence.map((e: any) => (
                <div key={e.id} className="evidence-entry">
                  <div className="evidence-header">
                    <span className="evidence-title">{e.name ?? `Evidence #${e.id}`}</span>
                    <span className="evidence-type">{e.type ?? 'Unknown'}</span>
                  </div>
                  <p className="evidence-description">{e.description ?? 'No description'}</p>
                  {e.properties && (
                    <div className="evidence-properties">
                      {Object.entries(e.properties).map(([key, value]) => (
                        <span key={key} className="property-tag">{key}: {String(value)}</span>
                      ))}
                    </div>
                  )}
                </div>
              ))
            )}
          </div>
        )}

        {tab === 'log' && (
          <div className="action-log">
            {(!player?.action_log || player.action_log.length === 0) ? (
              <p className="empty-state">No actions recorded yet.</p>
            ) : (
              [...player.action_log].reverse().map((entry, idx) => (
                <div key={idx} className="log-entry">
                  <span className="log-time">Tick {entry.tick}</span>
                  <span className="log-verb">{entry.verb}</span>
                  <span className="log-summary">{entry.summary}</span>
                  {entry.result && <span className="log-result">{entry.result}</span>}
                </div>
              ))
            )}
          </div>
        )}
      </div>

      <div className="journal-footer">
        <div className="player-vitals">
          <span>❤️ {Math.round(player?.health ?? 100)}</span>
          <span>😴 {Math.round(player?.fatigue ?? 0)}</span>
          <span>🍖 {Math.round(player?.hunger ?? 0)}</span>
          <span>⭐ {Math.round(player?.reputation ?? 0)}</span>
          <span>✨ XP: {player?.xp ?? 0} (Lv.{player?.level ?? 1})</span>
        </div>
      </div>
    </div>
  )
}