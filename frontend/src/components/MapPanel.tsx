import { useEffect, useRef, useState } from 'react'
import type { NPC, WorldState } from '../types'

interface MapPanelProps {
  state: WorldState
  onTalk: (npcId: number) => void
  onInspect: (npcId: number) => void
  onMove: (x: number, y: number) => void
}

const REGION_COLORS = ['#2d4a3a', '#1d4a3f', '#27455a', '#2b3a4a']
const CATEGORY_ICONS: Record<string, string> = {
  food: '🍞',
  tool: '🔨',
  weapon: '⚔️',
  material: '🪵',
  clothing: '🧥',
  book: '📜',
  evidence: '🔍',
}

export function MapPanel({ state, onTalk, onInspect, onMove }: MapPanelProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const [hover, setHover] = useState<string | null>(null)
  const [selected, setSelected] = useState<number | null>(null)

  const player = state.player
  const playerRegionId = player?.region_id ?? -1
  const region = state.world.regions.find((r) => r.id === playerRegionId)
  const hereBuildings = state.world.buildings.filter((b) => b.position.region_id === playerRegionId)
  const hereNpcs = state.world.npcs.filter((n) => n.position.region_id === playerRegionId)
  const hereItems = state.world.items.filter(
    (i) => i.position.region_id === playerRegionId && i.owner_id === 0,
  )

  // World viewport in world units (square, centered on the region center).
  // The village content clusters near the origin; a modest window keeps the
  // town, its NPCs, and the player clearly visible.
  const viewSize = 160

  const toCanvas = (x: number, y: number): { x: number; y: number } => {
    const c = canvasRef.current
    const w = c?.width ?? 800
    const scale = w / viewSize
    const cx = w / 2
    return { x: cx + x * scale, y: (c?.height ?? 600) / 2 + y * scale }
  }

  useEffect(() => {
    const c = canvasRef.current
    if (!c) return
    const ctx = c.getContext('2d')
    if (!ctx) return

    const dpr = window.devicePixelRatio || 1
    const w = c.clientWidth
    const h = c.clientHeight
    c.width = w * dpr
    c.height = h * dpr
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0)

    const scale = w / viewSize
    const cx = w / 2
    const cy = h / 2
    const wx = (px: number) => cx + px * scale
    const wy = (py: number) => cy + py * scale

    // Background
    ctx.fillStyle = '#1a2b22'
    ctx.fillRect(0, 0, w, h)

    // Region bounds box (clamped to view so huge regions don't draw an edge frame)
    if (region) {
      const bx = Math.max(0, wx(region.bounds.min_x))
      const by = Math.max(0, wy(region.bounds.min_y))
      const bx2 = Math.min(w, wx(region.bounds.max_x))
      const by2 = Math.min(h, wy(region.bounds.max_y))
      if (bx < bx2 && by < by2) {
        ctx.fillStyle = 'rgba(40, 60, 48, 0.7)'
        ctx.fillRect(bx, by, bx2 - bx, by2 - by)
        ctx.strokeStyle = 'rgba(120, 150, 120, 0.35)'
        ctx.strokeRect(bx, by, bx2 - bx, by2 - by)
      }
    }

    // Buildings
    for (const b of hereBuildings) {
      const bw = Math.max((b.bounds.max_x - b.bounds.min_x) * scale, 8)
      const bh = Math.max((b.bounds.max_y - b.bounds.min_y) * scale, 8)
      const bx = wx((b.bounds.min_x + b.bounds.max_x) / 2) - bw / 2
      const by = wy((b.bounds.min_y + b.bounds.max_y) / 2) - bh / 2
      ctx.fillStyle = `rgba(${REGION_COLORS[b.type % REGION_COLORS.length]}, 0.9)`
      ctx.fillRect(bx, by, bw, bh)
      ctx.strokeStyle = 'rgba(210, 190, 150, 0.6)'
      ctx.lineWidth = 1
      ctx.strokeRect(bx, by, bw, bh)
      if (bw > 40) {
        ctx.fillStyle = '#d8c9a3'
        ctx.font = '11px system-ui'
        ctx.textAlign = 'center'
        ctx.fillText(b.name, bx + bw / 2, by + bh / 2 + 4)
      }
    }

    // Items
    for (const it of hereItems) {
      const p = toCanvas(it.position.x, it.position.y)
      ctx.font = '14px system-ui'
      ctx.textAlign = 'center'
      ctx.fillText(CATEGORY_ICONS[it.category] ?? '·', p.x, p.y + 5)
    }

    // NPCs
    for (const n of hereNpcs) {
      const p = toCanvas(n.position.x, n.position.y)
      ctx.beginPath()
      ctx.arc(p.x, p.y, 6, 0, Math.PI * 2)
      ctx.fillStyle = n.tier === 0 ? '#e8a33d' : '#a9cbb7'
      ctx.fill()
      ctx.strokeStyle = '#1a2b22'
      ctx.lineWidth = 1
      ctx.stroke()
      ctx.fillStyle = '#eee'
      ctx.font = '9px system-ui'
      ctx.textAlign = 'center'
      ctx.fillText(n.name.split(' ')[0], p.x, p.y - 9)
    }

    // Player
    if (player) {
      const p = toCanvas(player.position.x, player.position.y)
      ctx.beginPath()
      ctx.arc(p.x, p.y, 9, 0, Math.PI * 2)
      ctx.fillStyle = '#5b8dff'
      ctx.fill()
      ctx.strokeStyle = '#fff'
      ctx.lineWidth = 2
      ctx.stroke()
    }
  })

  const handleClick = (e: React.MouseEvent<HTMLCanvasElement>) => {
    const c = canvasRef.current
    if (!c) return
    const rect = c.getBoundingClientRect()
    const x = e.clientX - rect.left
    const y = e.clientY - rect.top
    const scale = c.clientWidth / viewSize
    const wx = (x - c.clientWidth / 2) / scale
    const wy = (y - c.clientHeight / 2) / scale

    // Nearest NPC within click radius
    let best: NPC | null = null
    let bestDist = 30 / scale
    for (const n of hereNpcs) {
      const d = Math.hypot(n.position.x - wx, n.position.y - wy)
      if (d < bestDist) {
        bestDist = d
        best = n
      }
    }
    if (best) {
      setSelected(best.id)
      setHover(`${best.name} ${best.surname} — ${best.occupation}`)
      return
    }

    // Nearest building (informational hover, no move into it)
    let bBest: string | null = null
    for (const b of hereBuildings) {
      if (wx >= b.bounds.min_x && wx <= b.bounds.max_x && wy >= b.bounds.min_y && wy <= b.bounds.max_y) {
        bBest = b.name
      }
    }
    if (bBest) {
      setHover(bBest)
      return
    }

    // Otherwise: move the player to that spot
    onMove(wx, wy)
    setSelected(null)
    setHover(null)
  }

  const selectedNpc = hereNpcs.find((n) => n.id === selected) ?? null

  return (
    <div className="panel map-panel">
      <h3>
        {region?.name ?? 'Unknown region'}
        <span className="map-hint">click to move · click a villager to talk</span>
      </h3>
      <canvas
        ref={canvasRef}
        className="map-canvas"
        onClick={handleClick}
        onMouseLeave={() => setHover(null)}
      />
      {hover && <div className="map-hover">{hover}</div>}
      {selectedNpc && (
        <div className="map-actions">
          <button onClick={() => onTalk(selectedNpc.id)}>Talk</button>
          <button onClick={() => onInspect(selectedNpc.id)}>Inspect</button>
          <button onClick={() => setSelected(null)}>✕</button>
        </div>
      )}
    </div>
  )
}
