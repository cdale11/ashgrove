import { useEffect, useRef, useState } from 'react'
import type { Building, NPC, WorldState } from '../types'

interface MapPanelProps {
  state: WorldState
  onTalk: (npcId: number) => void
  onInspect: (npcId: number) => void
  onMove: (x: number, y: number) => void
  onEnter: (buildingId: number) => void
  onPickup: (itemId: number) => void
  onPlant: (plotId: number, crop: string) => void
  onWater: (plotId: number) => void
  onHarvest: (plotId: number) => void
  onFish: (spotId: number) => void
  onWork: (jobId: number) => void
  onFocus: (target: {
    type: 'building' | 'npc' | 'item'
    id: number
    name: string
    description: string
    sensory?: string[]
    knowledge?: string[]
  } | null) => void
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
// Crop stage -> glyph + plot tint (matches world::CropStage order).
const CROP_STAGE_GLYPHS: Record<number, string> = {
  0: '⬚', // empty
  1: '🌱', // planted
  2: '🌿', // sprouting
  3: '🌿', // growing
  4: '🌻', // flowering
  5: '🌾', // ready
  6: '🥀', // withered
}
const CROP_STAGE_TINTS: Record<number, string> = {
  0: '#4a3b23',
  1: '#5d7a3a',
  2: '#6d8f42',
  3: '#7f9d4c',
  4: '#8a9d55',
  5: '#c9a93f',
  6: '#5a4a38',
}

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

export function MapPanel({ state, onTalk, onInspect, onMove, onEnter, onPickup, onPlant, onWater, onHarvest, onFish, onWork, onFocus }: MapPanelProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const [hover, setHover] = useState<string | null>(null)
  const [selected, setSelected] = useState<number | null>(null)
  const [selectedBuilding, setSelectedBuilding] = useState<number | null>(null)
  const [selectedItem, setSelectedItem] = useState<number | null>(null)
  const [selectedPlot, setSelectedPlot] = useState<number | null>(null)
  const [selectedSpot, setSelectedSpot] = useState<number | null>(null)
  const [selectedJob, setSelectedJob] = useState<number | null>(null)

  const player = state.player
  const playerRegionId = player?.region_id ?? -1
  const region = state.world.regions.find((r) => r.id === playerRegionId)
  const hereBuildings = state.world.buildings.filter((b) => b.position.region_id === playerRegionId)
  const hereNpcs = state.world.npcs.filter((n) => n.position.region_id === playerRegionId)
  const hereItems = state.world.items.filter(
    (i) => i.position.region_id === playerRegionId && i.owner_id === 0,
  )
  const herePlots = (state.world.crop_plots ?? []).filter((p) => p.region_id === playerRegionId)
  const hereSpots = (state.world.fishing_spots ?? []).filter((s) => s.region_id === playerRegionId)
  const hereJobs = (state.world.job_postings ?? []).filter((j) => j.region_id === playerRegionId && j.is_active)

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
      if (b.id === selectedBuilding) {
        ctx.strokeStyle = '#ffe066'
        ctx.lineWidth = 3
        ctx.strokeRect(bx - 3, by - 3, bw + 6, bh + 6)
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

    // Crop plots: dark tilled soil square + stage glyph + moisture hint.
    for (const pl of herePlots) {
      const p = toCanvas(pl.position.x, pl.position.y)
      const size = Math.max(8, 3.2 * scale)
      ctx.fillStyle = CROP_STAGE_TINTS[pl.stage] ?? '#4a3b23'
      ctx.fillRect(p.x - size, p.y - size, size * 2, size * 2)
      ctx.strokeStyle = 'rgba(120, 90, 50, 0.7)'
      ctx.lineWidth = 1
      ctx.strokeRect(p.x - size, p.y - size, size * 2, size * 2)
      if (pl.stage > 0) {
        ctx.fillStyle = '#fff'
        ctx.font = '11px system-ui'
        ctx.textAlign = 'center'
        ctx.fillText(CROP_STAGE_GLYPHS[pl.stage] ?? '🌱', p.x, p.y + 4)
      }
      if (pl.stage > 0 && pl.water_level < 0.25 && pl.stage !== 6) {
        ctx.fillStyle = 'rgba(120, 200, 255, 0.5)'
        ctx.fillText('!', p.x + size, p.y - size)
      }
      if (pl.id === selectedPlot) {
        ctx.strokeStyle = '#ffe066'
        ctx.lineWidth = 2
        ctx.strokeRect(p.x - size - 2, p.y - size - 2, size * 2 + 4, size * 2 + 4)
      }
    }

    // Fishing spots: dark water pool + ripple rings + glyph.
    for (const s of hereSpots) {
      const p = toCanvas(s.position.x, s.position.y)
      const r = Math.max(10, 4 * scale)
      ctx.fillStyle = 'rgba(24, 70, 110, 0.35)'
      ctx.beginPath()
      ctx.arc(p.x, p.y, r, 0, Math.PI * 2)
      ctx.fill()
      ctx.strokeStyle = 'rgba(140, 190, 230, 0.4)'
      ctx.lineWidth = 1
      for (let ri = 1; ri <= 2; ri++) {
        ctx.beginPath()
        ctx.arc(p.x, p.y, r * (0.35 + ri * 0.25), 0, Math.PI * 2)
        ctx.stroke()
      }
      ctx.fillStyle = '#bfe3ff'
      ctx.font = '12px system-ui'
      ctx.textAlign = 'center'
      ctx.fillText('🐟', p.x, p.y + 4)
      if (s.id === selectedSpot) {
        ctx.strokeStyle = '#ffe066'
        ctx.lineWidth = 2
        ctx.beginPath()
        ctx.arc(p.x, p.y, r + 2, 0, Math.PI * 2)
        ctx.stroke()
      }
    }

    // Job sites: small board marker with a coin glyph.
    for (const j of hereJobs) {
      const p = toCanvas(j.work_position.x, j.work_position.y)
      ctx.fillStyle = 'rgba(243, 156, 18, 0.2)'
      ctx.beginPath()
      ctx.arc(p.x, p.y, 10, 0, Math.PI * 2)
      ctx.fill()
      ctx.fillStyle = '#ffd97a'
      ctx.font = '12px system-ui'
      ctx.textAlign = 'center'
      ctx.fillText('💼', p.x, p.y + 4)
      if (j.id === selectedJob) {
        ctx.strokeStyle = '#ffe066'
        ctx.lineWidth = 2
        ctx.beginPath()
        ctx.arc(p.x, p.y, 12, 0, Math.PI * 2)
        ctx.stroke()
      }
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
    // FOG OF WAR: dim beyond a clear radius around the player. Kept mild so the
    // village stays readable; renders as a blue-dark falloff, not flat grey.
    // Insecurity shrinks the known world — the dread closes in.
    if (player) {
      const pp = toCanvas(player.position.x, player.position.y)
      const base = Math.min(w, h)
      const insecurity = state.time_data?.insecurity ?? 0
      const insecurityShrink = 0.05 + (insecurity / 100) * 0.25 // up to 30% smaller at max
      const radius = base * (0.46 + 0.28 * dayFactor - insecurityShrink)
      const fw = ctx.createRadialGradient(pp.x, pp.y, radius * 0.4, pp.x, pp.y, radius)
      fw.addColorStop(0, 'rgba(0,0,0,0)')
      fw.addColorStop(1, 'rgba(2, 4, 10, 0.55)')
      ctx.fillStyle = fw
      ctx.fillRect(0, 0, w, h)
    }
    // Night tint: a cool, deep blue so night reads as night, not grey-out.
    if (dayFactor < 1) {
      const darkness = 1 - dayFactor
      ctx.fillStyle = `rgba(8, 12, 40, ${0.38 * darkness})`
      ctx.fillRect(0, 0, w, h)
    }
    // Weather fog / murk: skip during deep night (fog over dark = washed grey).
    if (weatherIntensity > 0.05 && dayFactor > 0.3) {
      const murk = seasonTint === SEASON_TINTS.Winter ? '#dce4ec' : '#aebbb0'
      ctx.fillStyle = `rgba(${tintRgb(murk)[0]}, ${tintRgb(murk)[1]}, ${tintRgb(murk)[2]}, ${0.12 + weatherIntensity * 0.25})`
      ctx.fillRect(0, 0, w, h)
    }
    // Peripheral vignette for dread.
    const vg = ctx.createRadialGradient(cx, cy, Math.min(w, h) * 0.4, cx, cy, Math.max(w, h) * 0.72)
    vg.addColorStop(0, 'rgba(0,0,0,0)')
    vg.addColorStop(1, `rgba(2, 3, 8, ${0.22 + (1 - dayFactor) * 0.3})`)
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

    // Nearest building: click to inspect/enter.
    let bBest: Building | null = null
    for (const b of hereBuildings) {
      if (wx >= b.bounds.min_x && wx <= b.bounds.max_x && wy >= b.bounds.min_y && wy <= b.bounds.max_y) {
        bBest = b
      }
    }
    if (bBest) {
      setSelectedBuilding(bBest.id)
      setHover(bBest.name)
      return
    }
    setSelectedBuilding(null)

    // Nearest item (loose goods) – click to focus.
    let iBest: typeof hereItems[0] | null = null
    let bestDistItem = 20 / scale
    for (const it of hereItems) {
      const p = toCanvas(it.position.x, it.position.y)
      const d = Math.hypot(p.x - x, p.y - y)
      if (d < bestDistItem) {
        bestDistItem = d
        iBest = it
      }
    }
    if (iBest) {
      setSelectedItem(iBest.id)
      setHover(iBest.name)
      onFocus({
        type: 'item',
        id: iBest.id,
        name: iBest.name,
        description: iBest.properties?.description ?? `${iBest.category}: ${iBest.name}`,
        sensory: iBest.properties?.scent ? [`Scent of ${iBest.properties.scent}`] : undefined,
        knowledge: iBest.properties?.knowledge ? [iBest.properties.knowledge] : undefined,
      })
      return
    }
    setSelectedItem(null)

    // Crop plot click
    let pBest: (typeof herePlots)[0] | null = null
    let bestDistPlot = 25 / scale
    for (const pl of herePlots) {
      const d = Math.hypot(pl.position.x - wx, pl.position.y - wy)
      if (d < bestDistPlot) {
        bestDistPlot = d
        pBest = pl
      }
    }
    if (pBest) {
      setSelectedPlot(pBest.id)
      setSelectedSpot(null)
      setSelectedJob(null)
      setHover(`Crop plot (stage ${pBest.stage}) — water ${Math.round(pBest.water_level * 100)}%`)
      return
    }
    setSelectedPlot(null)

    // Fishing spot click
    let sBest: (typeof hereSpots)[0] | null = null
    let bestDistSpot = 30 / scale
    for (const s of hereSpots) {
      const d = Math.hypot(s.position.x - wx, s.position.y - wy)
      if (d < bestDistSpot) {
        bestDistSpot = d
        sBest = s
      }
    }
    if (sBest) {
      setSelectedSpot(sBest.id)
      setSelectedJob(null)
      setHover(sBest.name)
      return
    }
    setSelectedSpot(null)

    // Job marker click
    let jBest: (typeof hereJobs)[0] | null = null
    let bestDistJob = 25 / scale
    for (const j of hereJobs) {
      const d = Math.hypot(j.work_position.x - wx, j.work_position.y - wy)
      if (d < bestDistJob) {
        bestDistJob = d
        jBest = j
      }
    }
    if (jBest) {
      setSelectedJob(jBest.id)
      setHover(jBest.title)
      return
    }
    setSelectedJob(null)

    // Otherwise: move the player to that spot
    onMove(wx, wy)
    setSelected(null)
    setHover(null)
  }

  const selectedNpc = hereNpcs.find((n) => n.id === selected) ?? null
  const selectedItemObj = hereItems.find((i) => i.id === selectedItem) ?? null

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
          <button
            onClick={() => {
              onFocus({
                type: 'npc',
                id: selectedNpc.id,
                name: `${selectedNpc.name} ${selectedNpc.surname}`,
                description: `${selectedNpc.occupation}. ${selectedNpc.name} looks ${selectedNpc.current_emotion === 1 ? 'pleased' : selectedNpc.current_emotion === 2 ? 'angry' : selectedNpc.current_emotion === 3 ? 'fearful' : 'neutral'}.`,
                sensory: [`Voice: ${selectedNpc.gender === 'female' ? 'soft' : 'gruff'}`, `Smells of ${selectedNpc.occupation === 'farmer' ? 'earth' : selectedNpc.occupation === 'smith' ? 'iron and soot' : 'old paper'}`],
              })
            }}
          >
            Focus
          </button>
          <button onClick={() => setSelected(null)}>✕</button>
        </div>
      )}
      {selectedBuilding !== null && (
        <div className="map-actions">
          <button
            onClick={() => {
              setSelectedBuilding(null)
              setHover(null)
              onEnter(selectedBuilding)
            }}
          >
            Enter
          </button>
          <button
            onClick={() => {
              const b = hereBuildings.find((bb) => bb.id === selectedBuilding)
              if (b) {
                onFocus({
                  type: 'building',
                  id: b.id,
                  name: b.name,
                  description: b.description,
                  sensory: ['Dust motes in slant light', 'Faint draft from cracks'],
                })
              }
            }}
          >
            Focus
          </button>
          <button onClick={() => setSelectedBuilding(null)}>✕</button>
        </div>
      )}
      {selectedItemObj && (
        <div className="map-actions">
          <button
            onClick={() => {
              onPickup(selectedItemObj.id)
              setSelectedItem(null)
              setHover(null)
            }}
          >
            Pick up
          </button>
          <button
            onClick={() => {
              onFocus({
                type: 'item',
                id: selectedItemObj.id,
                name: selectedItemObj.name,
                description: selectedItemObj.properties?.description ?? `${selectedItemObj.category}: ${selectedItemObj.name}`,
                sensory: selectedItemObj.properties?.scent ? [`Scent of ${selectedItemObj.properties.scent}`] : undefined,
                knowledge: selectedItemObj.properties?.knowledge ? [selectedItemObj.properties.knowledge] : undefined,
              })
            }}
          >
            Focus
          </button>
          <button onClick={() => setSelectedItem(null)}>✕</button>
        </div>
      )}
      {selectedPlot !== null && (() => {
        const pl = herePlots.find((p) => p.id === selectedPlot)
        if (!pl) return null
        const ready = pl.stage === 5
        const planted = pl.stage > 0 && pl.stage < 6
        return (
          <div className="map-actions">
            {pl.stage === 0 && (
              <>
                <button onClick={() => { onPlant(pl.id, 'wheat'); setHover('Planting wheat...') }}>Plant wheat</button>
                <button onClick={() => { onPlant(pl.id, 'carrots'); setHover('Planting carrots...') }}>Plant carrots</button>
                <button onClick={() => { onPlant(pl.id, 'potatoes'); setHover('Planting potatoes...') }}>Plant potatoes</button>
              </>
            )}
            {planted && pl.water_level < 1 && (
              <button onClick={() => { onWater(pl.id); setHover('Watering...') }}>Water</button>
            )}
            {ready && (
              <button className="map-act-harvest" onClick={() => { onHarvest(pl.id); setSelectedPlot(null); setHover(null) }}>Harvest</button>
            )}
            <button onClick={() => setSelectedPlot(null)}>✕</button>
          </div>
        )
      })()}
      {selectedSpot !== null && (() => {
        const s = hereSpots.find((sp) => sp.id === selectedSpot)
        if (!s) return null
        return (
          <div className="map-actions">
            <button onClick={() => { onFish(s.id); setHover('Fishing...') }}>Fish here</button>
            <button onClick={() => setSelectedSpot(null)}>✕</button>
          </div>
        )
      })()}
      {selectedJob !== null && (() => {
        const j = hereJobs.find((jj) => jj.id === selectedJob)
        if (!j) return null
        return (
          <div className="map-actions">
            <button onClick={() => { onWork(j.id); setHover('Working a shift...') }}>Work shift</button>
            <button onClick={() => setSelectedJob(null)}>✕</button>
          </div>
        )
      })()}
    </div>
  )
}
