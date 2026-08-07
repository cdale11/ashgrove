import type { NPC, WorldState } from '../types'

interface InteriorPanelProps {
  state: WorldState
  buildingId: number
  onTalk: (npcId: number) => void
  onInspect: (npcId: number) => void
  onExit: () => void
}

export function InteriorPanel({ state, buildingId, onTalk, onInspect, onExit }: InteriorPanelProps) {
  const building = state.world.buildings.find((b) => b.id === buildingId)

  if (!building) {
    return (
      <div className="panel map-panel">
        <h3>Inside a building</h3>
        <p>The building seems to have vanished.</p>
        <div className="map-actions">
          <button onClick={onExit}>Exit</button>
        </div>
      </div>
    )
  }

  const playerRegion = state.player?.region_id ?? -1

  // People associated with the building (residents / workers / keeper) who are
  // currently present in the same region -> "inside" this interior.
  const associatedIds = new Set([
    ...building.resident_ids,
    ...building.worker_ids,
    building.owner_id,
  ])
  const inside = state.world.npcs.filter(
    (n) => associatedIds.has(n.id) && n.position.region_id === playerRegion,
  )

  // Loose goods resting at the building (owner-agnostic world items near it).
  const items = state.world.items.filter(
    (it) =>
      it.position.region_id === playerRegion &&
      Math.hypot(it.position.x - (building.position as unknown as { x: number }).x, it.position.y - (building.position as unknown as { y: number }).y) < 22,
  )

  return (
    <div className="panel map-panel interior">
      <h3>
        Inside · {building.name}
        <span className="map-hint">
          {building.level > 1 && `Lv ${building.level} · `}
          {inside.length} inside
        </span>
      </h3>

      {building.description && <p className="interior-desc">{building.description}</p>}

      {inside.length > 0 && (
        <div className="interior-group">
          <h4>People</h4>
          <ul className="interior-list">
            {inside.map((n) => (
              <li key={n.id} className="interior-person">
                <span>
                  {n.name} {n.surname}
                  <span className="map-hint tab">— {n.occupation}</span>
                </span>
                <span className="map-actions">
                  <button onClick={() => onTalk(n.id)}>Talk</button>
                  <button onClick={() => onInspect(n.id)}>Inspect</button>
                </span>
              </li>
            ))}
          </ul>
        </div>
      )}

      {items.length > 0 && (
        <div className="interior-group">
          <h4>Kept here</h4>
          <ul className="interior-items">
            {items.map((it) => (
              <li key={it.id}>· {it.name}</li>
            ))}
          </ul>
        </div>
      )}

      <div className="map-actions">
        <button className="interior-exit" onClick={onExit}>→ Exit to the village</button>
      </div>
    </div>
  )
}