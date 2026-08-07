import type { WorldState } from '../types'

const BUILDING_TYPE_NAMES = [
  'Residence',
  'Shop',
  'Workshop',
  'Civic',
  'Religious',
  'Farm',
  'Ruin',
]

export function WorldPanel({ state }: { state: WorldState }) {
  const { buildings, regions, items } = state.world

  return (
    <div className="panel world-panel">
      <h3>World</h3>

      <div className="detail-section">
        <h4>Regions ({regions.length})</h4>
        <ul className="region-list">
          {regions.map((r) => (
            <li key={r.id} className="region-entry">
              <span className="region-name">{r.name}</span>
              <span className="region-danger" style={{ opacity: 0.3 + r.danger_level }}>
                Danger {Math.round(r.danger_level * 100)}%
              </span>
              <span className="region-count">
                {r.building_ids.length} buildings · {r.npc_ids.length} NPCs
              </span>
            </li>
          ))}
        </ul>
      </div>

      <div className="detail-section">
        <h4>Buildings ({buildings.length})</h4>
        <ul className="building-list">
          {buildings.map((b) => (
            <li key={b.id} className="building-entry">
              <span className="building-type badge">
                {BUILDING_TYPE_NAMES[b.type] ?? 'Unknown'}
              </span>
              <span className="building-name">{b.name}</span>
              <span className="building-condition">
                {Math.round(b.condition * 100)}%
              </span>
            </li>
          ))}
        </ul>
      </div>

      <div className="detail-section">
        <h4>Artifacts ({items.length})</h4>
        <ul className="item-list">
          {items.map((item) => (
            <li key={item.id} className="item-entry">
              <span className="item-name">{item.name}</span>
              <span className="item-category">{item.category}</span>
              <span className="item-value">${Math.round(item.value)}</span>
            </li>
          ))}
          {items.length === 0 && <li className="empty">No loose items</li>}
        </ul>
      </div>
    </div>
  )
}