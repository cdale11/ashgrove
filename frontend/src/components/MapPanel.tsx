import { useEffect, useRef, useState } from 'react'
import type { NPC, WorldState } from '../types'

interface MapPanelProps {
  state: WorldState
  onTalk: (npcId: number) => void
  onInspect: (npcId: number) => void
  onMove: (x: number, y: number) => void
}

// Per-season ground palettes (base grass tones before tonal correction).
const SEASON_TINTS: Record<string, string> = {
  Spring: '#3a5c44',
  Summer: '#4a7048',
  Autumn: '#7a5c38',
  Winter: '#c9d4dd',
  Default: '#3a5244',
}
const CATEGORY_ICONS: Record<string, string> = {
  food: '🍞',
  tool: '🔨',
  weapon: '⚔️',
  material: '🪵',
  clothing: '🧥',
  book: '📜',
  evidence: '🔍',
}
// Building type -> sprite + label color (matches world::BuildingType order).
const BUILDING_SPRITES: Record<string, string> = {
  0: '🏚️', // mill
  1: '🏪', // shop / inn
  2: '⚒️', // forge
  3: '🏛️', // town hall
  4: '⛪', // chapel
}
// Occupation keyword -> villager glyph.
const NPC_GLYPHS: Record<string, string> = {
  trader: '🧑‍🌾',
  farmer: '🌾',
  smith: '🛠️',
  priest: '🕯️',
  innkeep: '🍻',
  guard: '🛡️',
  carpenter: '🪚',
  teacher: '📖',
  elder: '🧓',
}
const DEFAULT_NPC_GLYPH = '🧑'
const SPRITES = {
  player: '🧭',
  object: '🧩',
  path: '#9a7b5a',
}

// Small deterministic PRNG so the terrain looks the same every render.
function mulberry32(seed: number) {
  let a = seed >>> 0
  return function () {
    a |= 0
    a = (a + 0x6d2b79f5) | 0
    let t = Math.imul(a ^ (a >>> 15), 1 | a)
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296
  }
}
function pickGlyph(n: NPC): string {
  const occ = (n.occupation ?? '').toLowerCase()
  for (const k of Object.keys(NPC_GLYPHS)) if (occ.includes(k)) return NPC_GLYPHS[k]
  return DEFAULT_NPC_GLYPH
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

    // Time of day determines light level (0 = darkest night, 1 = full day).
    const hour = state.time_data?.hour ?? 12
    const daylight = hour >= 7 && hour < 19
    const dayFactor = daylight ? 1 : hour >= 6 && hour < 21 ? 0.55 : 0.18
    const seasonTint = SEASON_TINTS[state.time_data?.season ?? 'Default'] ?? SEASON_TINTS.Default
    const weatherIntensity = state.time_data?.weather_intensity ?? 0

    // Parse season tint into rgb for blending
    const tintRgb = (hex: string): [number, number, number] =>
      [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16)) as [number, number, number]
    const [tr, tg, tb] = tintRgb(seasonTint)

    // ---- Scene: terrain + entities (everything beneath atmosphere) ----
    const rng = mulberry32((region?.id ?? 1) * 7919 + 17)

    // Tar vertically-striped light-dark grass patches as the base terrain.
    ctx.fillStyle = `rgb(${Math.round(tr * dayFactor)}, ${Math.round(tg * dayFactor)}, ${Math.round(tb * dayFactor)})`
    ctx.fillRect(0, 0, w, h)
    ctx.globalAlpha = 0.18
    for (let i = 0; i < 220; i++) {
      const gx = rng() * w
      const gy = rng() * h
      const gr = 6 + rng() * 26
      ctx.fillStyle = rng() > 0.5 ? '#0a1a0c' : '#0f2a14'
      ctx.beginPath()
      ctx.ellipse(gx, gy, gr, gr * (0.5 + rng()), rng() * Math.PI, 0, Math.PI * 2)
      ctx.fill()
    }
    ctx.globalAlpha = 1

    // Treeline ringing the region, plus scattered sparse trees.
    ctx.font = `${Math.max(12, 6 * scale)}px system-ui`
    ctx.textAlign = 'center'
    for (let i = 0; i < 90; i++) {
      const bx = rng() * viewSize - viewSize / 2
      const by = rng() * viewSize - viewSize / 2
      const nx = wx(bx)
      const ny = wy(by)
      ctx.globalAlpha = 0.9
      ctx.fillText('🌲', nx, ny)
    }
    ctx.globalAlpha = 1

    // Dirt footpaths: a crude network from each building to the central hub.
    let hub = hereBuildings[0]
    let hubBest = Infinity
    for (const b of hereBuildings) {
      const d = Math.hypot((b.bounds.min_x + b.bounds.max_x) / 2, (b.bounds.min_y + b.bounds.max_y) / 2)
      if (d < hubBest) {
        hubBest = d
        hub = b
      }
    }
    ctx.strokeStyle = SPRITES.path
    ctx.lineWidth = Math.max(2, 1.2 * scale)
    ctx.lineCap = 'round'
    ctx.globalAlpha = 0.7
    if (hub) {
      for (const b of hereBuildings) {
        if (b.id === hub.id) continue
        const ax = wx((b.bounds.min_x + b.bounds.max_x) / 2)
        const ay = wy((b.bounds.min_y + b.bounds.max_y) / 2)
        const hx = wx((hub.bounds.min_x + hub.bounds.max_x) / 2)
        const hy = wy((hub.bounds.min_y + hub.bounds.max_y) / 2)
        ctx.beginPath()
        ctx.moveTo(ax, ay)
        ctx.lineTo(hx, hy)
        ctx.stroke()
      }
    }
    ctx.globalAlpha = 1

    // Buildings: roofed box + facade + sprite + name.
    for (const b of hereBuildings) {
      const bw = Math.max((b.bounds.max_x - b.bounds.min_x) * scale, 22)
      const bh = Math.max((b.bounds.max_y - b.bounds.min_y) * scale, 22)
      const bx = wx((b.bounds.min_x + b.bounds.max_x) / 2) - bw / 2
      const by = wy((b.bounds.min_y + b.bounds.max_y) / 2) - bh / 2
      // Soft shadow
      ctx.fillStyle = 'rgba(0,0,0,0.18)'
      ctx.fillRect(bx + 3, by + 4, bw, bh)
      // Facade
      ctx.fillStyle = `rgb(${Math.round(200 * dayFactor)}, ${Math.round(190 * dayFactor + 8)}, ${Math.round(160 * dayFactor + 6)})`
      ctx.fillRect(bx, by, bw, bh)
      ctx.strokeStyle = `rgba(${Math.round(120 + 60 * (1 - dayFactor))}, ${Math.round(90 + 40 * (1 - dayFactor))}, 60, 0.9)`
      ctx.lineWidth = 2
      ctx.strokeRect(bx, by, bw, bh)
      // Roof
      ctx.fillStyle = `rgb(${Math.round(150 * dayFactor)}, ${Math.round(80 * dayFactor + 6)}, ${Math.round(80 * dayFactor + 10)})`
      ctx.beginPath()
      ctx.moveTo(bx - 4, by + 4)
      ctx.lineTo(bx + bw / 2, by - Math.min(bh * 0.28, 14))
      ctx.lineTo(bx + bw + 4, by + 4)
      ctx.closePath()
      ctx.fill()
      ctx.stroke()
      // Sprite + name
      ctx.fillStyle = '#fff'
      ctx.font = '10px system-ui'
      ctx.textAlign = 'center'
      ctx.fillText(BUILDING_SPRITES[b.type] ?? '🏠', bx + bw / 2, by - Math.max(8, bh * 0.28))
      if (bw > 30) {
        ctx.fillStyle = '#f4ead0'
        ctx.font = '9px system-ui'
        ctx.fillText(b.name, bx + bw / 2, by + bh + 11)
      }
    }

    // Items: drop a faint glow halo under the loose goods.
    for (const it of hereItems) {
      const p = toCanvas(it.position.x, it.position.y)
      ctx.fillStyle = 'rgba(255, 240, 180, 0.18)'
      ctx.beginPath()
      ctx.arc(p.x, p.y, 8, 0, Math.PI * 2)
      ctx.fill()
      ctx.fillStyle = '#fff'
      ctx.font = '14px system-ui'
      ctx.textAlign = 'center'
      ctx.fillText(CATEGORY_ICONS[it.category] ?? '·', p.x, p.y + 5)
    }

    // NPCs: shadow + glyph + name + selection ring.
    for (const n of hereNpcs) {
      const p = toCanvas(n.position.x, n.position.y)
      if (n.tier === 0) {
        ctx.fillStyle = 'rgba(232, 163, 61, 0.24)'
        ctx.beginPath()
        ctx.arc(p.x, p.y, 11, 0, Math.PI * 2)
        ctx.fill()
      }
      ctx.fillStyle = '#fff'
      ctx.font = '13px system-ui'
      ctx.textAlign = 'center'
      ctx.fillText(pickGlyph(n), p.x, p.y + 5)
      if (n.id === selected) {
        ctx.strokeStyle = '#ffe066'
        ctx.lineWidth = 2
        ctx.beginPath()
        ctx.arc(p.x, p.y, 11, 0, Math.PI * 2)
        ctx.stroke()
      }
      ctx.fillStyle = '#eee'
      ctx.font = '9px system-ui'
      ctx.fillText(n.name.split(' ')[0], p.x, p.y - 12)
    }

    // Player: compass glyph over a lit marker.
    if (player) {
      const p = toCanvas(player.position.x, player.position.y)
      ctx.fillStyle = 'rgba(91, 141, 255, 0.25)'
      ctx.beginPath()
      ctx.arc(p.x, p.y, 11, 0, Math.PI * 2)
      ctx.fill()
      ctx.fillStyle = '#fff'
      ctx.font = '15px system-ui'
      ctx.textAlign = 'center'
      ctx.fillText(SPRITES.player, p.x, p.y + 5)
    }

    // --- Atmosphere overlays ---
    // FOG OF WAR: dim everything beyond a clear radius around the player
    // (the horror: the known world shrinks as night deepens).
    if (player) {
      const pp = toCanvas(player.position.x, player.position.y)
      const base = Math.min(w, h)
      const radius = base * (0.42 + 0.3 * dayFactor)
      const fw = ctx.createRadialGradient(pp.x, pp.y, radius * 0.25, pp.x, pp.y, radius)
      fw.addColorStop(0, 'rgba(5, 8, 14, 0)')
      fw.addColorStop(1, 'rgba(5, 8, 14, 0.92)')
      ctx.fillStyle = fw
      ctx.fillRect(0, 0, w, h)
    }
    // Night tint.
    if (dayFactor < 1) {
      const darkness = 1 - dayFactor
      ctx.fillStyle = `rgba(10, 14, 34, ${0.5 * darkness})`
      ctx.fillRect(0, 0, w, h)
    }
    // Weather fog / murk.
    if (weatherIntensity > 0.05) {
      let murk
      if (weatherIntensity >= 0.6) murk = '#cfd8da'
      else murk = seasonTint === SEASON_TINTS.Winter ? '#dfe6ec' : '#b9c3b2'
      ctx.fillStyle = `rgba(${tintRgb(murk)[0]}, ${tintRgb(murk)[1]}, ${tintRgb(murk)[2]}, ${Math.min(0.55, 0.1 + weatherIntensity * 0.45)})`
      ctx.fillRect(0, 0, w, h)
    }
    // Peripheral vignette for dread.
    const vg = ctx.createRadialGradient(cx, cy, Math.min(w, h) * 0.34, cx, cy, Math.max(w, h) * 0.72)
    vg.addColorStop(0, 'rgba(0,0,0,0)')
    vg.addColorStop(1, `rgba(4, 6, 12, ${0.2 + (1 - dayFactor) * 0.5})`)
    ctx.fillStyle = vg
    ctx.fillRect(0, 0, w, h)
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
