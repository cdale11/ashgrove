import { useState } from 'react'
import type { WorldState } from '../types'

interface PlayerPanelProps {
  state: WorldState
  act: (action: Record<string, unknown>) => Promise<Record<string, unknown>>
  onNotice: (msg: string) => void
  onTalk: (npcId: number) => void
  onInspect: (npcId: number) => void
}

export function PlayerPanel({ state, act, onNotice, onTalk, onInspect }: PlayerPanelProps) {
  const player = state.player
  const [busy, setBusy] = useState<string | null>(null)

  if (!player) return null

  const regionName = (id: number): string =>
    state.world.regions.find((r) => r.id === id)?.name ?? `Region ${id}`
  const currentRegion = regionName(player.region_id)

  const destinations: Array<{ key: string; label: string; x: number; y: number; z: number; region?: string }> = [
    ...state.world.buildings
      .filter((b) => b.position.region_id === player.region_id)
      .map((b) => ({
        key: `b:${b.id}:${b.position.x}:${b.position.y}`,
        label: `Walk to ${b.name}`,
        x: b.position.x,
        y: b.position.y,
        z: b.position.z,
      })),
    // Cross-region travel — all current players may be entities, but the player
    // is the traveler; villages connect to the woods and the river.
    ...state.world.regions
      .filter((r) => r.id !== player.region_id)
      .map((r) => ({
        key: `r:${r.id}`,
        label: `Cross into ${r.name}`,
        x: 0,
        y: 0,
        z: 0,
        region: r.name,
      })),
  ]

  const npcsHere = state.world.npcs.filter((n) => n.position.region_id === player.region_id)
  const pickables = state.world.items.filter(
    (i) =>
      i.owner_id === 0 &&
      i.position.region_id === player.region_id &&
      !player.inventory.includes(i.id),
  )
  const inventoryItems = state.world.items.filter((i) => player.inventory.includes(i.id))

  const run = async (key: string, fn: () => Promise<void>) => {
    if (busy) return
    setBusy(key)
    try {
      await fn()
    } finally {
      setBusy(null)
    }
  }

  const doMove = (d: (typeof destinations)[number]) =>
    run(d.key, async () => {
      const res = await act({
        type: 'move',
        target: { x: d.x, y: d.y, z: d.z },
        region: d.region ?? undefined,
      })
      onNotice(String(res.message ?? res.error ?? 'Moved'))
    })

  const doTake = (itemId: number, itemPos: { x: number; y: number; z: number }) =>
    run(`take:${itemId}`, async () => {
      // Walk over first (movement is instant in the slice), then take.
      await act({ type: 'move', target: { x: itemPos.x, y: itemPos.y, z: itemPos.z } })
      const res = await act({ type: 'pickup', target: itemId })
      onNotice(String(res.message ?? res.error ?? 'Picked up'))
    })

  const doRest = () =>
    run('rest', async () => {
      const res = await act({ type: 'rest' })
      onNotice(String(res.message ?? res.error ?? 'Resting'))
    })

  const bar = (v: number, color: string) => (
    <div className="bar">
      <div className="bar-fill" style={{ width: `${Math.min(100, Math.max(0, v))}%`, background: color }} />
    </div>
  )

  return (
    <div className="panel player-panel">
      <h3>{player.name}</h3>
      <div className="player-location">
        <span>{currentRegion}</span>
        <span className="position">({Math.round(player.position.x)}, {Math.round(player.position.y)})</span>
        {player.resting && <span className="state-tag">resting</span>}
      </div>

      <div className="npc-vitals">
        <div className="vital"><span className="vital-label">Health</span>{bar(player.health, '#27ae60')}</div>
        <div className="vital"><span className="vital-label">Hunger</span>{bar(player.hunger, player.hunger > 60 ? '#c0392b' : '#e67e22')}</div>
        <div className="vital"><span className="vital-label">Fatigue</span>{bar(player.fatigue, '#8e44ad')}</div>
        <div className="vital"><span className="vital-label">Standing</span>{bar((player.reputation + 100) / 2, player.reputation >= 0 ? '#27ae60' : '#c0392b')}</div>
      </div>

      <div className="action-group">
        <h4>Move</h4>
        <div className="action-buttons">
          {destinations.map((d) => (
            <button key={d.key} onClick={() => doMove(d)} disabled={busy !== null}>
              {d.label}
            </button>
          ))}
        </div>
      </div>

      <div className="action-group">
        <h4>Talk &amp; inspection</h4>
        <ul className="npc-shortlist">
          {npcsHere.map((n) => (
            <li key={n.id} className="npc-short">
              <span className="npc-short-name">{n.name} {n.surname} — {n.occupation}</span>
              <span className="npc-short-actions">
                <button disabled={busy !== null} onClick={() => onTalk(n.id)}>Talk</button>
                <button disabled={busy !== null} onClick={() => onInspect(n.id)}>Inspect</button>
              </span>
            </li>
          ))}
          {npcsHere.length === 0 && <li className="empty">Nobody here.</li>}
        </ul>
      </div>

      <div className="action-group">
        <h4>Items</h4>
        {pickables.length > 0 && (
          <ul className="item-list">
            {pickables.map((i) => (
              <li key={i.id} className="pickup-row">
                <span className="item-name">{i.name}</span>
                <button disabled={busy !== null} onClick={() => doTake(i.id, i.position)}>Take</button>
              </li>
            ))}
          </ul>
        )}
        {inventoryItems.length > 0 && (
          <ul className="item-list">
            {inventoryItems.map((i) => (
              <li key={i.id} className="inventory-item">
                <span>{i.name}</span>
                <i>{i.category}</i>
              </li>
            ))}
          </ul>
        )}
        {pickables.length === 0 && inventoryItems.length === 0 && (
          <p className="empty">No items here.</p>
        )}
      </div>

      <div className="action-group">
        <h4>Rest</h4>
        <button className="rest-btn" onClick={doRest} disabled={busy !== null}>Sit down and rest</button>
      </div>
    </div>
  )
}