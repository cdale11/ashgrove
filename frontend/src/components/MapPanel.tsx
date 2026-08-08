import { useEffect, useMemo, useRef, useState } from 'react'
import type { Building, CropPlot, FishingSpot, Item, JobPosting, NPC, WorldState } from '../types'

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
// Building type -> sprite (matches world::BuildingType order).
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
const SPRITES = { player: '🧭', object: '🧩' }

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
// Deterministic hash in [0,1) from world coordinates (stable under camera moves).
function worldHash(ix: number, iy: number): number {
  let h = (ix * 374761393 + iy * 668265263) | 0
  h = (h ^ (h >> 13)) | 0
  h = Math.imul(h, 1274126177)
  h = (h ^ (h >> 16)) >>> 0
  return h / 4294967296
}
function pickGlyph(n: NPC): string {
  const occ = (n.occupation ?? '').toLowerCase()
  for (const k of Object.keys(NPC_GLYPHS)) if (occ.includes(k)) return NPC_GLYPHS[k]
  return DEFAULT_NPC_GLYPH
}
function weatherIcon(w?: string): string {
  const s = (w ?? '').toLowerCase()
  if (s.includes('storm')) return '⛈️'
  if (s.includes('rain')) return '🌧️'
  if (s.includes('snow')) return '❄️'
  if (s.includes('fog') || s.includes('mist')) return '🌫️'
  if (s.includes('cloud')) return '☁️'
  return '☀️'
}

// Rounded-rect helper (avoid relying on CanvasRenderingContext2D.roundRect).
function rr(ctx: CanvasRenderingContext2D, x: number, y: number, w: number, h: number, r: number) {
  const rad = Math.min(r, w / 2, h / 2)
  ctx.beginPath()
  ctx.moveTo(x + rad, y)
  ctx.arcTo(x + w, y, x + w, y + h, rad)
  ctx.arcTo(x + w, y + h, x, y + h, rad)
  ctx.arcTo(x, y + h, x, y, rad)
  ctx.arcTo(x, y, x + w, y, rad)
  ctx.closePath()
}

// Hand-drawn crop at any growth stage (world-space size `half` in px).
function drawCrop(ctx: CanvasRenderingContext2D, x: number, y: number, half: number, stage: number, now: number) {
  if (stage === 0) {
    ctx.strokeStyle = 'rgba(210, 240, 160, 0.55)'
    ctx.lineWidth = 1.2
    ctx.setLineDash([4, 3])
    ctx.strokeRect(x - half, y - half, half * 2, half * 2)
    ctx.setLineDash([])
    return
  }
  if (stage === 6) {
    ctx.fillStyle = 'rgba(122, 96, 62, 0.9)'
    ctx.beginPath()
    ctx.ellipse(x, y, half * 0.55, half * 0.38, 0, 0, Math.PI * 2)
    ctx.fill()
    ctx.strokeStyle = 'rgba(90, 70, 45, 0.9)'
    ctx.lineWidth = 1.5
    ctx.beginPath()
    ctx.moveTo(x - half * 0.3, y - half * 0.5)
    ctx.quadraticCurveTo(x, y - half * 0.2, x + half * 0.2, y + half * 0.1)
    ctx.stroke()
    return
  }
  const grow = stage === 1 ? 0.3 : stage === 2 ? 0.55 : stage === 3 ? 0.75 : 0.8
  const r = half * grow
  const cx = x
  const cy = y + half * 0.15
  ctx.fillStyle = stage >= 5 ? 'rgba(255, 214, 110, 0.28)' : 'rgba(255, 255, 180, 0.12)'
  ctx.beginPath()
  ctx.arc(cx, cy, half * 0.85, 0, Math.PI * 2)
  ctx.fill()
  if (stage === 1) {
    ctx.strokeStyle = '#7fae4f'
    ctx.lineWidth = 1.4
    ctx.beginPath()
    ctx.moveTo(cx, cy + r)
    ctx.lineTo(cx, cy - r * 0.6)
    ctx.stroke()
    for (const sgn of [-1, 1]) {
      ctx.fillStyle = '#86b85a'
      ctx.beginPath()
      ctx.ellipse(cx + sgn * r * 0.5, cy - r * 0.3, r * 0.5, r * 0.3, sgn * 0.5, 0, Math.PI * 2)
      ctx.fill()
    }
    return
  }
  ctx.fillStyle = stage >= 5 ? '#c8a63c' : '#5d8a3f'
  ctx.beginPath()
  ctx.arc(cx, cy - r * 0.35, r * 0.8, 0, Math.PI * 2)
  ctx.fill()
  ctx.fillStyle = stage >= 5 ? '#e8cf5e' : '#74a04e'
  ctx.beginPath()
  ctx.arc(cx - r * 0.25, cy - r * 0.5, r * 0.55, 0, Math.PI * 2)
  ctx.fill()
  ctx.beginPath()
  ctx.arc(cx + r * 0.3, cy - r * 0.2, r * 0.45, 0, Math.PI * 2)
  ctx.fill()
  if (stage >= 4) {
    for (const [fx, fy] of [[-r * 0.45, cy - r * 0.55], [r * 0.5, cy - r * 0.05]] as const) {
      ctx.fillStyle = stage >= 5 ? '#fff3c4' : '#f7f0d8'
      ctx.beginPath()
      ctx.arc(fx, fy, 1.8, 0, Math.PI * 2)
      ctx.fill()
      ctx.fillStyle = '#e8b83c'
      ctx.beginPath()
      ctx.arc(fx, fy, 0.9, 0, Math.PI * 2)
      ctx.fill()
    }
  }
  if (stage === 5) {
    const sway = Math.sin(now / 500) * 0.6
    ctx.strokeStyle = '#d9b43c'
    ctx.lineWidth = 1.3
    for (let i = -1; i <= 1; i++) {
      const ex = cx + i * half * 0.3
      ctx.beginPath()
      ctx.moveTo(cx + i * half * 0.12, cy + r * 0.4)
      ctx.quadraticCurveTo(ex, cy - r * 0.1, ex + sway, cy - r * 0.75)
      ctx.stroke()
      ctx.fillStyle = '#f0d476'
      ctx.beginPath()
      ctx.ellipse(ex + sway, cy - r * 0.8, 1.6, 3.4, 0.1, 0, Math.PI * 2)
      ctx.fill()
    }
  }
}

// World-space geometry extents of everything worth looking at (for camera clamp).
function contentBounds(lists: Array<Array<{ minX: number; minY: number; maxX: number; maxY: number }>>) {
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity
  for (const list of lists) for (const it of list) {
    minX = Math.min(minX, it.minX)
    minY = Math.min(minY, it.minY)
    maxX = Math.max(maxX, it.maxX)
    maxY = Math.max(maxY, it.maxY)
  }
  if (!isFinite(minX)) return { minX: -70, minY: -70, maxX: 70, maxY: 70 }
  return { minX, minY, maxX, maxY }
}

export function MapPanel({ state, onTalk, onInspect, onMove, onEnter, onPickup, onPlant, onWater, onHarvest, onFish, onWork, onFocus }: MapPanelProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const [hover, setHover] = useState<string | null>(null)
  const hoverRef = useRef<{ x: number; y: number; color: string } | null>(null)
  const [dragging, setDragging] = useState(false)
  const dragRef = useRef<null | { px: number; py: number; cx: number; cy: number; moved: boolean }>(null)
  const dragMovedRef = useRef(false)
  const [cam, setCam] = useState({ x: 0, y: 0 })
  const camInitialized = useRef(false)
  const [selected, setSelected] = useState<number | null>(null)
  const [selectedBuilding, setSelectedBuilding] = useState<number | null>(null)
  const [selectedItem, setSelectedItem] = useState<number | null>(null)
  const [selectedPlot, setSelectedPlot] = useState<number | null>(null)
  const [selectedSpot, setSelectedSpot] = useState<number | null>(null)
  const [selectedJob, setSelectedJob] = useState<number | null>(null)
  const [zoom, setZoom] = useState(1)

  const player = state.player
  const playerRegionId = player?.region_id ?? -1
  const region = state.world.regions.find((r) => r.id === playerRegionId)
  const hereBuildings: Building[] = state.world.buildings.filter((b) => b.position.region_id === playerRegionId)
  const hereNpcs: NPC[] = state.world.npcs.filter((n) => n.position.region_id === playerRegionId)
  const hereItems: Item[] = state.world.items.filter(
    (i) => i.position.region_id === playerRegionId && i.owner_id === 0,
  )
  const herePlots: CropPlot[] = (state.world.crop_plots ?? []).filter((p) => p.position.region_id === playerRegionId)
  const hereSpots: FishingSpot[] = (state.world.fishing_spots ?? []).filter((s) => s.position.region_id === playerRegionId)
  const hereJobs: JobPosting[] = (state.world.job_postings ?? []).filter((j) => j.region_id === playerRegionId && j.is_active)

  // Zoom: 1 = default view. The camera sits in world units; pan with drag.
  const baseView = 130

  // World-space extents of all interactive content (for camera clamp + terrain placement).
  const content = useMemo(() => {
    const lists = [
      hereBuildings.map((b) => ({ minX: b.bounds.min_x, minY: b.bounds.min_y, maxX: b.bounds.max_x, maxY: b.bounds.max_y })),
      herePlots.map((p) => ({ minX: p.position.x - 6, minY: p.position.y - 6, maxX: p.position.x + 6, maxY: p.position.y + 6 })),
      hereSpots.map((sp) => ({ minX: sp.position.x - 10, minY: sp.position.y - 10, maxX: sp.position.x + 10, maxY: sp.position.y + 10 })),
      hereJobs.map((j) => ({ minX: j.work_position.x - 6, minY: j.work_position.y - 6, maxX: j.work_position.x + 6, maxY: j.work_position.y + 6 })),
      hereNpcs.map((n) => ({ minX: n.position.x - 5, minY: n.position.y - 5, maxX: n.position.x + 5, maxY: n.position.y + 5 })),
      player ? [{ minX: player.position.x, minY: player.position.y, maxX: player.position.x, maxY: player.position.y }] : [],
    ]
    const b = contentBounds(lists)
    return { ...b, cx: (b.minX + b.maxX) / 2, cy: (b.minY + b.maxY) / 2 }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [state])

  useEffect(() => {
    if (!camInitialized.current && player) {
      camInitialized.current = true
      setCam({ x: player.position.x, y: player.position.y })
    }
  }, [player])

  // Uniform projection: world -> screen. Camera is the viewed world center.
  const proj = (x: number, y: number) => {
    const c = canvasRef.current
    const w = c?.clientWidth ?? 800
    const h = c?.clientHeight ?? 600
    const s = (h * zoom) / baseView
    const px = w / 2 + (x - cam.x) * s
    const py = h / 2 + (y - cam.y) * s
    return { x: px, y: py, s }
  }

  const clampCam = (x: number, y: number) => {
    const c = canvasRef.current
    const w = c?.clientWidth ?? 800
    const h = c?.clientHeight ?? 600
    const s = (h * zoom) / baseView
    const b = content
    const pad = 70
    return {
      x: Math.min(b.maxX + w / s / 2 + pad, Math.max(b.minX - w / s / 2 - pad, x)),
      y: Math.min(b.maxY + h / s / 2 + pad, Math.max(b.minY - h / s / 2 - pad, y)),
    }
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

    const s = (h * zoom) / baseView
    const cx = w / 2
    const cy = h / 2
    const wx = (px: number) => cx + (px - cam.x) * s
    const wy = (py: number) => cy + (py - cam.y) * s

    // Time of day determines light level (0 = darkest night, 1 = full day).
    const hour = state.time_data?.hour ?? 12
    const daylight = hour >= 7 && hour < 19
    const dayFactor = daylight ? 1 : hour >= 6 && hour < 21 ? 0.55 : 0.18
    const seasonTint = SEASON_TINTS[state.time_data?.season ?? 'Default'] ?? SEASON_TINTS.Default
    const seasonName = state.time_data?.season ?? 'Default'
    const weatherName = state.time_data?.weather ?? 'Clear'
    const weatherIntensity = state.time_data?.weather_intensity ?? 0
    const insecurity = state.time_data?.insecurity ?? 0

    // Parse season tint into rgb for blending.
    const tintRgb = (hex: string): [number, number, number] =>
      [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16)) as [number, number, number]
    const [tr, tg, tb] = tintRgb(seasonTint)

    const rng = mulberry32((region?.id ?? 1) * 7919 + 17)

    // ---- Static scene is painted into an offscreen canvas; the rAF loop
    // blits it and adds the living layer (weather, fireflies, shimmer). ----
    const off = document.createElement('canvas')
    off.width = c.width
    off.height = c.height
    const octx = off.getContext('2d')
    if (!octx) return
    octx.setTransform(dpr, 0, 0, dpr, 0, 0)

    // ----- Terrain: layered ground filled edge to edge (no letterbox). -----
    const viewW = w / s
    const viewH = h / s
    const groundTop = `rgb(${Math.round(tr * 1.18 * dayFactor + 8)}, ${Math.round(tg * 1.18 * dayFactor + 10)}, ${Math.round(tb * 1.08 * dayFactor + 8)})`
    const groundBottom = `rgb(${Math.round(tr * 0.72 * dayFactor)}, ${Math.round(tg * 0.72 * dayFactor)}, ${Math.round(tb * 0.72 * dayFactor)})`
    const grad = octx.createLinearGradient(0, 0, 0, h)
    grad.addColorStop(0, groundTop)
    grad.addColorStop(1, groundBottom)
    octx.fillStyle = grad
    octx.fillRect(0, 0, w, h)

    // Big soft moss patches, world-anchored (drawn across the visible window).
    for (let i = 0; i < 60; i++) {
      const px2 = cam.x + (rng() - 0.5) * viewW
      const py2 = cam.y + (rng() - 0.5) * viewH
      const gr = 8 + rng() * 38
      octx.fillStyle = rng() > 0.5 ? 'rgba(10, 26, 12, 0.09)' : 'rgba(28, 58, 30, 0.11)'
      octx.beginPath()
      octx.ellipse(wx(px2), wy(py2), gr, gr * (0.5 + rng() * 0.5), rng() * Math.PI, 0, Math.PI * 2)
      octx.fill()
    }

    // ----- Geography: hills, a meandering creek, boulders, wooded groves. -----
    const terr = mulberry32((region?.id ?? 1) * 9001 + 13)

    // Soft elevation mounds, kept clear of the occupied core.
    const hills: Array<{ x: number; y: number; rx: number; ry: number }> = []
    const tryHill = () => {
      const hx = content.minX - 120 + terr() * (content.maxX - content.minX + 240)
      const hy = content.minY - 130 + terr() * (content.maxY - content.minY + 260)
      const rx = 14 + terr() * 20
      const ry = 8 + terr() * 11
      const clear = hx + rx < content.minX - 6 || hx - rx > content.maxX + 6 || hy + ry < content.minY - 6 || hy - ry > content.maxY + 6
      if (clear) hills.push({ x: hx, y: hy, rx, ry })
    }
    for (let i = 0; i < 80 && hills.length < 9; i++) tryHill()
    for (const hill of hills) {
      const hp = proj(hill.x, hill.y)
      const hx = hill.rx * s
      const hy = hill.ry * s
      octx.fillStyle = 'rgba(12, 26, 18, 0.1)'
      octx.beginPath()
      octx.ellipse(hp.x, hp.y, hx * 1.2, hy * 1.2, 0, 0, Math.PI * 2)
      octx.fill()
      octx.fillStyle = 'rgba(255, 246, 214, 0.14)'
      octx.beginPath()
      octx.ellipse(hp.x - hx * 0.16, hp.y - hy * 0.24, hx * 0.9, hy * 0.88, 0, 0, Math.PI * 2)
      octx.fill()
      octx.fillStyle = 'rgba(4, 12, 6, 0.14)'
      octx.beginPath()
      octx.ellipse(hp.x + hx * 0.24, hp.y + hy * 0.28, hx * 0.6, hy * 0.46, 0, 0, Math.PI * 2)
      octx.fill()
    }

    // A creek wandering across the north-east fringe of the region.
    const creekTop = content.minY - 26
    const creek: Array<[number, number]> = []
    for (let i = 0; i <= 8; i++) {
      const t = i / 8
      const x = content.minX - 130 + t * (content.maxX - content.minX + 260)
      const y = creekTop + Math.sin(t * Math.PI * 2.4) * 14 + (terr() - 0.5) * 7
      creek.push([x, y])
    }
    octx.lineCap = 'round'
    octx.lineJoin = 'round'
    const creekW = Math.max(10, 4.6 * s)
    octx.strokeStyle = 'rgba(176, 140, 92, 0.45)'
    octx.lineWidth = creekW * 1.9
    octx.beginPath()
    octx.moveTo(wx(creek[0][0]), wy(creek[0][1]))
    for (let i = 1; i < creek.length; i++) octx.lineTo(wx(creek[i][0]), wy(creek[i][1]))
    octx.stroke()
    octx.strokeStyle = 'rgba(52, 118, 158, 0.6)'
    octx.lineWidth = creekW
    octx.beginPath()
    octx.moveTo(wx(creek[0][0]), wy(creek[0][1]))
    for (let i = 1; i < creek.length; i++) octx.lineTo(wx(creek[i][0]), wy(creek[i][1]))
    octx.stroke()
    octx.strokeStyle = 'rgba(140, 200, 228, 0.4)'
    octx.lineWidth = creekW * 0.45
    octx.beginPath()
    octx.moveTo(wx(creek[0][0]), wy(creek[0][1]))
    for (let i = 1; i < creek.length; i++) octx.lineTo(wx(creek[i][0]), wy(creek[i][1]))
    octx.stroke()
    // Pebbles hugging the creek bed.
    for (let i = 0; i < 16; i++) {
      const t = terr()
      const ci = Math.floor(t * (creek.length - 1))
      const frac = t * (creek.length - 1) - ci
      const x = creek[ci][0] + (creek[ci + 1][0] - creek[ci][0]) * frac
      const y = creek[ci][1] + (creek[ci + 1][1] - creek[ci][1]) * frac
      const cp = proj(x + (terr() - 0.5) * 7, y + (terr() - 0.5) * 7)
      octx.fillStyle = 'rgba(108, 102, 90, 0.6)'
      octx.beginPath()
      octx.arc(cp.x, cp.y, 1.2 + terr() * 1.4, 0, Math.PI * 2)
      octx.fill()
    }

    // Boulders scattered across open ground (never on top of buildings).
    const boulders: Array<{ x: number; y: number; r: number }> = []
    for (let i = 0; i < 60 && boulders.length < 18; i++) {
      const bx = content.minX - 220 + terr() * (content.maxX - content.minX + 440)
      const by = content.minY - 220 + terr() * (content.maxY - content.minY + 440)
      if (bx > content.minX - 8 && bx < content.maxX + 8 && by > content.minY - 8 && by < content.maxY + 8) continue
      boulders.push({ x: bx, y: by, r: 1.6 + terr() * 2.6 })
    }
    for (const bk of boulders) {
      const bp = proj(bk.x, bk.y)
      const br = bk.r * s
      octx.fillStyle = 'rgba(0, 0, 0, 0.18)'
      octx.beginPath()
      octx.ellipse(bp.x + 2, bp.y + 2.5, br, br * 0.7, 0, 0, Math.PI * 2)
      octx.fill()
      const rockFace = (off: number) => {
        octx.beginPath()
        for (let k = 0; k < 6; k++) {
          const a = off + (k / 6) * Math.PI * 2
          const rr2 = br * (0.7 + ((k * 37) % 29) / 38)
          const px3 = bp.x + Math.cos(a) * rr2
          const py3 = bp.y + Math.sin(a) * rr2 * 0.8
          if (k === 0) octx.moveTo(px3, py3)
          else octx.lineTo(px3, py3)
        }
        octx.closePath()
      }
      octx.fillStyle = '#47443f'
      rockFace(0.5)
      octx.fill()
      octx.fillStyle = '#5d5952'
      rockFace(2.2)
      octx.fill()
      octx.strokeStyle = 'rgba(12, 12, 9, 0.5)'
      octx.lineWidth = 1
      octx.stroke()
      octx.fillStyle = 'rgba(150, 148, 136, 0.45)'
      octx.beginPath()
      octx.arc(bp.x - br * 0.3, bp.y - br * 0.32, br * 0.24, 0, Math.PI * 2)
      octx.fill()
    }

    // Wooded groves on the fringes (denser than the treeline).
    for (const g of [
      { x: content.minX - 88, y: content.minY - 58, r: 30, n: 15 },
      { x: content.maxX + 86, y: content.maxY + 34, r: 34, n: 16 },
    ]) {
      for (let i = 0; i < g.n; i++) {
        const a = terr() * Math.PI * 2
        const d = Math.sqrt(terr()) * g.r
        const gx = wx(g.x + Math.cos(a) * d)
        const gy = wy(g.y + Math.sin(a) * d * 0.8)
        const gr = 4.5 + terr() * 6
        octx.fillStyle = 'rgba(40, 56, 34, 0.5)'
        octx.beginPath()
        octx.arc(gx + 1.6, gy + 2, gr, 0, Math.PI * 2)
        octx.fill()
        octx.fillStyle = seasonName === 'Winter' ? '#5d6a70' : '#2c4524'
        octx.beginPath()
        octx.arc(gx, gy, gr, 0, Math.PI * 2)
        octx.fill()
        octx.fillStyle = seasonName === 'Winter' ? '#7a858c' : '#476a30'
        octx.beginPath()
        octx.arc(gx - gr * 0.3, gy - gr * 0.3, gr * 0.62, 0, Math.PI * 2)
        octx.fill()
        if (i % 3 === 0) {
          octx.fillStyle = '#6b4a30'
          octx.fillRect(gx - 1.2, gy + gr * 0.3, 2.4, gr * 0.55)
        }
      }
    }

    // World-lattice decoration: grass tufts, stones, flowers keyed by cell.
    const cell = 11
    const c0x = Math.floor((cam.x - viewW / 2) / cell)
    const c1x = Math.ceil((cam.x + viewW / 2) / cell)
    const c0y = Math.floor((cam.y - viewH / 2) / cell)
    const c1y = Math.ceil((cam.y + viewH / 2) / cell)
    const flowerColors = seasonName === 'Winter' ? null
      : seasonName === 'Autumn' ? ['#d98b2b', '#c96f22', '#e0a63e', '#b85c22']
      : seasonName === 'Summer' ? ['#f2d94c', '#e8b83c', '#e57a4f']
      : ['#f0e6e0', '#e8a9c2', '#f2d94c']
    for (let ix = c0x; ix <= c1x; ix++) {
      for (let iy = c0y; iy <= c1y; iy++) {
        const hh = worldHash(ix, iy)
        const hx = ix * cell + (hh % 0.47) * cell
        const hy = iy * cell + ((hh * 7919) % 0.47) * cell
        const d = hh * 100
        // Clumps: grass tufts or small stones or flowers.
        if (hh > 0.42) {
          octx.fillStyle = seasonName === 'Winter'
            ? 'rgba(235, 242, 248, 0.3)'
            : `rgba(${Math.round(tr + 34)}, ${Math.round(tg + 46)}, ${Math.round(tb + 26)}, 0.32)`
          octx.beginPath()
          octx.arc(wx(hx), wy(hy), 1.4, 0, Math.PI * 2)
          octx.fill()
        }
        if (flowerColors && hh < 0.16) {
          octx.fillStyle = flowerColors[Math.floor(d % flowerColors.length)]
          octx.beginPath()
          octx.arc(wx(hx + (hh * 13 % 0.4) * cell * 0.5), wy(hy + 2), 1.6, 0, Math.PI * 2)
          octx.fill()
        }
        if (hh > 0.86) {
          // Occasional stone.
          octx.fillStyle = 'rgba(96, 88, 78, 0.55)'
          octx.beginPath()
          octx.ellipse(wx(hx), wy(hy), 2.6, 2, hh * 3, 0, Math.PI * 2)
          octx.fill()
          octx.fillStyle = 'rgba(140, 132, 120, 0.4)'
          octx.beginPath()
          octx.ellipse(wx(hx - 0.8), wy(hy - 0.7), 1.1, 0.9, 0, 0, Math.PI * 2)
          octx.fill()
        }
      }
    }
    // Grass-blade strokes spread across the window (world-anchored).
    for (let i = 0; i < 320; i++) {
      const gx = cam.x + (rng() - 0.5) * viewW
      const gy = cam.y + (rng() - 0.5) * viewH
      const gl = 2 + rng() * 4
      octx.strokeStyle = seasonName === 'Winter'
        ? `rgba(235, 242, 248, ${0.14 + rng() * 0.18})`
        : `rgba(${Math.round(tr + 44)}, ${Math.round(tg + 56)}, ${Math.round(tb + 30)}, ${0.1 + rng() * 0.16})`
      octx.lineWidth = 1
      octx.beginPath()
      octx.moveTo(wx(gx), wy(gy))
      octx.lineTo(wx(gx) + (rng() - 0.5) * 2, wy(gy) - gl)
      octx.stroke()
    }

    // ----- Scattered woodland: trees keyed by world cells grow naturally
    // across the whole map (no artificial ring around the village).
    const treeCell = 15
    for (let ix = Math.floor((cam.x - viewW / 2) / treeCell); ix <= Math.ceil((cam.x + viewW / 2) / treeCell); ix++) {
      for (let iy = Math.floor((cam.y - viewH / 2) / treeCell); iy <= Math.ceil((cam.y + viewH / 2) / treeCell); iy++) {
        const th = worldHash(ix * 29 + 11, iy * 41 + 7)
        if (th > 0.055) continue
        const tx = ix * treeCell + ((th * 0.37) % 1) * treeCell
        const ty = iy * treeCell + ((th * 0.61) % 1) * treeCell
        const tsx = wx(tx)
        const tsy = wy(ty)
        const tc = 4 + ((th * 53) % 1) * 8
        octx.fillStyle = 'rgba(48, 64, 40, 0.45)'
        octx.beginPath()
        octx.arc(tsx + 1.5, tsy + 2, tc, 0, Math.PI * 2)
        octx.fill()
        octx.fillStyle = seasonName === 'Winter' ? '#5d6a70' : '#2f4a2a'
        octx.beginPath()
        octx.arc(tsx, tsy, tc, 0, Math.PI * 2)
        octx.fill()
        octx.fillStyle = seasonName === 'Winter' ? '#77838a' : '#3d5d33'
        octx.beginPath()
        octx.arc(tsx - tc * 0.3, tsy - tc * 0.25, tc * 0.72, 0, Math.PI * 2)
        octx.fill()
        octx.fillStyle = '#6b4a30'
        octx.fillRect(tsx - 1, tsy + tc * 0.35, 2, tc * 0.5)
      }
    }

    // ----- Footpaths: two-tone dirt with gentle curves to the hub. -----
    let hub: Building | null = null
    let hubBest = Infinity
    for (const b of hereBuildings) {
      const d = Math.hypot((b.bounds.min_x + b.bounds.max_x) / 2, (b.bounds.min_y + b.bounds.max_y) / 2)
      if (d < hubBest) {
        hubBest = d
        hub = b
      }
    }
    if (hub) {
      const hx = wx((hub.bounds.min_x + hub.bounds.max_x) / 2)
      const hy = wy((hub.bounds.min_y + hub.bounds.max_y) / 2)
      for (const b of hereBuildings) {
        if (b.id === hub.id) continue
        const ax = wx((b.bounds.min_x + b.bounds.max_x) / 2)
        const ay = wy((b.bounds.min_y + b.bounds.max_y) / 2)
        const mx = wx(((b.bounds.min_x + b.bounds.max_x) / 2 + (hub.bounds.min_x + hub.bounds.max_x) / 2) / 2 + (rng() - 0.5) * 7)
        const my = wy(((b.bounds.min_y + b.bounds.max_y) / 2 + (hub.bounds.min_y + hub.bounds.max_y) / 2) / 2 + (rng() - 0.5) * 7)
        octx.lineCap = 'round'
        octx.lineJoin = 'round'
        octx.strokeStyle = 'rgba(88, 68, 46, 0.55)'
        octx.lineWidth = Math.max(6, 2.4 * s)
        octx.beginPath()
        octx.moveTo(ax, ay)
        octx.quadraticCurveTo(mx, my, hx, hy)
        octx.stroke()
        octx.strokeStyle = 'rgba(146, 116, 74, 0.5)'
        octx.lineWidth = Math.max(2.5, 1 * s)
        octx.beginPath()
        octx.moveTo(ax, ay)
        octx.quadraticCurveTo(mx, my, hx, hy)
        octx.stroke()
        for (let i = 0; i < 5; i++) {
          const t = 0.1 + rng() * 0.8
          const px = (1 - t) * (1 - t) * ax + 2 * (1 - t) * t * mx + t * t * hx
          const py = (1 - t) * (1 - t) * ay + 2 * (1 - t) * t * my + t * t * hy
          octx.fillStyle = 'rgba(200, 170, 120, 0.5)'
          octx.beginPath()
          octx.arc(px, py, 1.2, 0, Math.PI * 2)
          octx.fill()
        }
      }
      // Trail from town to the nearest farm field.
      if (herePlots.length > 0) {
        let fcx = 0
        let fcy = 0
        for (const pl of herePlots) {
          fcx += pl.position.x
          fcy += pl.position.y
        }
        fcx /= herePlots.length
        fcy /= herePlots.length
        const fpx = wx(fcx)
        const fpy = wy(fcy)
        const mx = wx((fcx + (hub.bounds.min_x + hub.bounds.max_x) / 2) / 2 + (rng() - 0.5) * 6)
        const my = wy((fcy + (hub.bounds.min_y + hub.bounds.max_y) / 2) / 2 + (rng() - 0.5) * 6)
        octx.lineCap = 'round'
        octx.lineJoin = 'round'
        octx.strokeStyle = 'rgba(88, 68, 46, 0.5)'
        octx.lineWidth = Math.max(6, 2.4 * s)
        octx.beginPath()
        octx.moveTo(hx, hy)
        octx.quadraticCurveTo(mx, my, fpx, fpy)
        octx.stroke()
        octx.strokeStyle = 'rgba(146, 116, 74, 0.45)'
        octx.lineWidth = Math.max(2.5, 1 * s)
        octx.beginPath()
        octx.moveTo(hx, hy)
        octx.quadraticCurveTo(mx, my, fpx, fpy)
        octx.stroke()
      }
    }

    // ----- Farm fields: fenced soil with crops, signpost label. -----
    // Drawn before buildings so houses stand on the soil, not under it.
    const clusters: Array<Array<(typeof herePlots)[number]>> = []
    for (const pl of herePlots) {
      let placed = false
      for (const cl of clusters) {
        const ref = cl[0]
        if (Math.hypot(pl.position.x - ref.position.x, pl.position.y - ref.position.y) < 12) {
          cl.push(pl)
          placed = true
          break
        }
      }
      if (!placed) clusters.push([pl])
    }
    for (const cl of clusters) {
      let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity
      for (const pl of cl) {
        minX = Math.min(minX, pl.position.x)
        minY = Math.min(minY, pl.position.y)
        maxX = Math.max(maxX, pl.position.x)
        maxY = Math.max(maxY, pl.position.y)
      }
      const pad = 4.5
      const nw = proj(minX - pad, minY - pad)
      const se = proj(maxX + pad, maxY + pad)
      const fw = se.x - nw.x
      const fh = se.y - nw.y
      octx.fillStyle = 'rgba(96, 74, 44, 0.82)'
      rr(octx, nw.x, nw.y, fw, fh, 4)
      octx.fill()
      octx.strokeStyle = 'rgba(70, 54, 32, 0.7)'
      octx.lineWidth = 1.4
      octx.stroke()
      octx.strokeStyle = 'rgba(50, 38, 22, 0.55)'
      octx.lineWidth = 1
      for (let fy = nw.y + 5; fy < se.y - 3; fy += Math.max(6, 1.6 * s)) {
        octx.beginPath()
        octx.moveTo(nw.x + 3, fy)
        octx.lineTo(se.x - 3, fy)
        octx.stroke()
      }
      // Wooden fence border around the field.
      octx.strokeStyle = 'rgba(126, 100, 64, 0.65)'
      octx.lineWidth = 1.4
      octx.setLineDash([4, 5])
      octx.strokeRect(nw.x - 4, nw.y - 4, fw + 8, fh + 8)
      octx.setLineDash([])
      octx.fillStyle = '#5b4229'
      for (const [cxp, cyp] of [[nw.x, nw.y], [se.x, nw.y], [nw.x, se.y], [se.x, se.y]] as const) {
        octx.fillRect(cxp - 2, cyp - 2, 4, 4)
      }
      // Signpost, flipped below the field when a building is in the way.
      const signOverlaps = hereBuildings.some(
        (b) => b.bounds.min_x < maxX + pad && b.bounds.max_x > minX - pad && b.bounds.min_y < maxY + pad && b.bounds.max_y > minY - pad,
      )
      const sign = proj((minX + maxX) / 2, signOverlaps ? maxY + pad + 2.4 : minY - pad - 2.2)
      octx.strokeStyle = '#4c361f'
      octx.lineWidth = 2
      octx.beginPath()
      octx.moveTo(sign.x, sign.y + 4)
      octx.lineTo(sign.x, sign.y - 8)
      octx.stroke()
      octx.fillStyle = 'rgba(36, 26, 14, 0.78)'
      rr(octx, sign.x - 26, sign.y - 12, 52, 12, 3)
      octx.fill()
      octx.fillStyle = '#f0dba0'
      octx.font = 'bold 8.5px system-ui'
      octx.textAlign = 'center'
      octx.fillText(cl.length >= 3 ? '🌾 Thorne Farm' : '🌾 Old Mill garden', sign.x, sign.y - 3.5)
    }
    for (const pl of herePlots) {
      const p = proj(pl.position.x, pl.position.y)
      // Tiles sized to the actual plot spacing (4 world units) so they never
      // overlap each other, even when zoomed in.
      const half = Math.max(5, 2.0 * s)
      const bedGrad = octx.createLinearGradient(0, p.y - half, 0, p.y + half)
      bedGrad.addColorStop(0, '#6b5130')
      bedGrad.addColorStop(1, '#54401f')
      octx.fillStyle = bedGrad
      rr(octx, p.x - half, p.y - half, half * 2, half * 2, 2.5)
      octx.fill()
      octx.strokeStyle = 'rgba(30, 20, 10, 0.5)'
      octx.lineWidth = 1
      octx.stroke()
      drawCrop(octx, p.x, p.y, half, pl.stage, 0)
      if (pl.stage > 0 && pl.stage < 6 && pl.water_level < 0.6) {
        const bw = half * 2
        octx.fillStyle = 'rgba(0, 0, 0, 0.5)'
        octx.fillRect(p.x - half, p.y + half + 2.5, bw, 3)
        octx.fillStyle = pl.water_level < 0.25 ? '#e74c3c' : '#5dade2'
        octx.fillRect(p.x - half, p.y + half + 2.5, bw * Math.max(0.08, pl.water_level), 3)
      }
      if (pl.id === selectedPlot) {
        octx.strokeStyle = '#ffe066'
        octx.lineWidth = 2
        octx.setLineDash([5, 3])
        octx.strokeRect(p.x - half - 3, p.y - half - 3, half * 2 + 6, half * 2 + 6)
        octx.setLineDash([])
      }
    }

    // ----- Buildings: grounded with earth-shadow, stone base, roof, door. -----
    for (const b of hereBuildings) {
      const bw = Math.max((b.bounds.max_x - b.bounds.min_x) * s, 14)
      const bh = Math.max((b.bounds.max_y - b.bounds.min_y) * s, 14)
      const bx = wx((b.bounds.min_x + b.bounds.max_x) / 2) - bw / 2
      const by = wy((b.bounds.min_y + b.bounds.max_y) / 2) - bh / 2
      // Ground shadow: an oval cast onto the earth, anchoring the structure.
      octx.fillStyle = 'rgba(0, 0, 0, 0.3)'
      octx.beginPath()
      octx.ellipse(bx + bw / 2, by + bh + 3, bw * 0.72, bh * 0.22, 0, 0, Math.PI * 2)
      octx.fill()
      octx.fillStyle = 'rgba(0, 0, 0, 0.14)'
      octx.fillRect(bx + 1, by + bh - 1, bw - 2, 2)
      const wall = octx.createLinearGradient(0, by, 0, by + bh)
      wall.addColorStop(0, `rgb(${Math.round(206 * dayFactor + 10)}, ${Math.round(196 * dayFactor + 12)}, ${Math.round(168 * dayFactor + 8)})`)
      wall.addColorStop(1, `rgb(${Math.round(176 * dayFactor)}, ${Math.round(166 * dayFactor)}, ${Math.round(140 * dayFactor)})`)
      octx.fillStyle = wall
      octx.fillRect(bx, by, bw, bh)
      octx.strokeStyle = `rgba(${Math.round(90 + 40 * (1 - dayFactor))}, ${Math.round(70 + 30 * (1 - dayFactor))}, 46, 0.9)`
      octx.lineWidth = 1.5
      octx.strokeRect(bx, by, bw, bh)
      const roofH = Math.min(bh * 0.32, 13)
      const roof = octx.createLinearGradient(0, by - roofH, 0, by + 4)
      roof.addColorStop(0, `rgb(${Math.round(170 * dayFactor + 12)}, ${Math.round(96 * dayFactor + 8)}, ${Math.round(94 * dayFactor + 12)})`)
      roof.addColorStop(1, `rgb(${Math.round(120 * dayFactor)}, ${Math.round(62 * dayFactor + 4)}, ${Math.round(64 * dayFactor + 8)})`)
      octx.fillStyle = roof
      octx.beginPath()
      octx.moveTo(bx - 4, by + 3)
      octx.lineTo(bx + bw / 2, by - roofH - 3)
      octx.lineTo(bx + bw + 4, by + 3)
      octx.closePath()
      octx.fill()
      octx.strokeStyle = 'rgba(58, 30, 30, 0.8)'
      octx.lineWidth = 1.2
      octx.stroke()
      octx.strokeStyle = 'rgba(58, 30, 30, 0.5)'
      octx.beginPath()
      octx.moveTo(bx + bw / 2, by - roofH - 3)
      octx.lineTo(bx + bw / 2, by + 3)
      octx.stroke()
      const doorW = Math.max(bw * 0.26, 5)
      const doorH = bh * 0.5
      octx.fillStyle = '#3c2b1c'
      octx.fillRect(bx + bw / 2 - doorW / 2, by + bh - doorH, doorW, doorH)
      octx.fillStyle = 'rgba(0,0,0,0.25)'
      octx.fillRect(bx + bw / 2 - doorW / 2, by + bh - doorH, doorW, 2)
      // Door sill (threshold flat on the ground).
      octx.fillStyle = '#8a7352'
      octx.fillRect(bx + bw / 2 - doorW / 2, by + bh, doorW, 1.8)
      // Stone foundation hugging the base of the walls.
      octx.fillStyle = 'rgba(104, 96, 88, 0.95)'
      octx.fillRect(bx, by + bh - 2.5, bw, 2.5)
      octx.fillStyle = 'rgba(78, 72, 66, 0.9)'
      octx.fillRect(bx, by + bh - 2.5, bw, 0.9)
      const winW = Math.max(bw * 0.14, 3.5)
      const winH = Math.max(bh * 0.2, 4)
      for (const wxOff of [bw * 0.2, bw * 0.8]) {
        const lit = !daylight ? 'rgba(255, 206, 118, 0.95)' : 'rgba(96, 116, 140, 0.75)'
        octx.fillStyle = lit
        octx.fillRect(bx + wxOff - winW / 2, by + bh * 0.32, winW, winH)
        octx.strokeStyle = 'rgba(40, 28, 20, 0.7)'
        octx.lineWidth = 1
        octx.strokeRect(bx + wxOff - winW / 2, by + bh * 0.32, winW, winH)
        if (!daylight) {
          octx.strokeStyle = 'rgba(255, 210, 130, 0.25)'
          octx.lineWidth = 3
          octx.strokeRect(bx + wxOff - winW / 2 - 2, by + bh * 0.32 - 2, winW + 4, winH + 4)
        }
      }
      octx.fillStyle = '#fff'
      octx.font = '11px system-ui'
      octx.textAlign = 'center'
      octx.fillText(BUILDING_SPRITES[b.type] ?? '🏠', bx + bw / 2, by - roofH - 4)
      if (bw > 26) {
        octx.font = '9px system-ui'
        const tw = octx.measureText(b.name).width
        octx.fillStyle = 'rgba(18, 14, 8, 0.66)'
        rr(octx, bx + bw / 2 - tw / 2 - 4, by + bh + 5, tw + 8, 11, 3)
        octx.fill()
        octx.fillStyle = '#f4ead0'
        octx.fillText(b.name, bx + bw / 2, by + bh + 13)
      }
      if (b.id === selectedBuilding) {
        octx.strokeStyle = '#ffe066'
        octx.lineWidth = 2.5
        octx.strokeRect(bx - 4, by - roofH - 6, bw + 8, bh + roofH + 8)
      }
    }

    // ----- Fishing lakes: sandy shore, layered water, reeds, lily pads. -----
    const lakeR = Math.max(14, 4.6 * s)
    for (const spot of hereSpots) {
      const p = proj(spot.position.x, spot.position.y)
      // Sandy shore.
      octx.fillStyle = 'rgba(168, 132, 92, 0.45)'
      octx.beginPath()
      octx.ellipse(p.x, p.y, lakeR * 1.75, lakeR * 1.4, 0, 0, Math.PI * 2)
      octx.fill()
      // Deep and bright water layers.
      octx.fillStyle = 'rgba(32, 64, 104, 0.78)'
      octx.beginPath()
      octx.ellipse(p.x, p.y, lakeR * 1.5, lakeR * 1.16, 0, 0, Math.PI * 2)
      octx.fill()
      octx.fillStyle = 'rgba(58, 122, 168, 0.62)'
      octx.beginPath()
      octx.ellipse(p.x, p.y, lakeR * 1.05, lakeR * 0.8, 0, 0, Math.PI * 2)
      octx.fill()
      octx.fillStyle = 'rgba(108, 184, 216, 0.48)'
      octx.beginPath()
      octx.ellipse(p.x - lakeR * 0.15, p.y - lakeR * 0.2, lakeR * 0.5, lakeR * 0.36, 0, 0, Math.PI * 2)
      octx.fill()
      // Ripple ring.
      octx.strokeStyle = 'rgba(225, 242, 255, 0.3)'
      octx.lineWidth = 1
      octx.beginPath()
      octx.ellipse(p.x, p.y, lakeR * 0.8, lakeR * 0.6, 0, 0, Math.PI * 2)
      octx.stroke()
      // Shore stones.
      for (let k = 0; k < 7; k++) {
        const a2 = (k / 7) * Math.PI * 2 + 0.4
        const sx = p.x + Math.cos(a2) * lakeR * 1.52
        const sy = p.y + Math.sin(a2) * lakeR * 1.16
        octx.fillStyle = 'rgba(118, 112, 100, 0.8)'
        octx.beginPath()
        octx.ellipse(sx, sy, 2 + k * 0.3, 1.4 + k * 0.2, a2, 0, Math.PI * 2)
        octx.fill()
      }
      // Reeds on the north-west shore.
      for (let k = 0; k < 5; k++) {
        const a3 = Math.PI * (1.02 + k * 0.12)
        const bx = p.x + Math.cos(a3) * lakeR * 1.3
        const by = p.y + Math.sin(a3) * lakeR * 1.0
        octx.strokeStyle = '#3d5a33'
        octx.lineWidth = 1.3
        octx.beginPath()
        octx.moveTo(bx, by + lakeR * 0.28)
        octx.quadraticCurveTo(bx + 2, by, bx + 3.5, by - lakeR * 0.3)
        octx.stroke()
        octx.fillStyle = 'rgba(58, 92, 44, 0.9)'
        octx.beginPath()
        octx.ellipse(bx + 3.5, by - lakeR * 0.34, 1, 2.2, 0.2, 0, Math.PI * 2)
        octx.fill()
      }
      // Lily pads.
      for (const [lpdx, lpdy, lpr] of [[-lakeR * 0.55, lakeR * 0.38, 4.5], [lakeR * 0.5, -lakeR * 0.22, 3.6]] as const) {
        octx.fillStyle = 'rgba(40, 82, 48, 0.85)'
        octx.beginPath()
        octx.ellipse(p.x + lpdx, p.y + lpdy, lpr * 2, lpr, 0, 0, Math.PI * 2)
        octx.fill()
        octx.strokeStyle = 'rgba(14, 38, 22, 0.6)'
        octx.lineWidth = 1
        octx.beginPath()
        octx.ellipse(p.x + lpdx, p.y + lpdy, lpr * 2, lpr, 0, 0, Math.PI * 2)
        octx.stroke()
        octx.fillStyle = '#e8a9c2'
        octx.beginPath()
        octx.arc(p.x + lpdx + lpr * 0.6, p.y + lpdy - lpr * 0.3, 1.2, 0, Math.PI * 2)
        octx.fill()
      }
      // Bobber + fish glyph.
      octx.fillStyle = '#f0f4f8'
      octx.beginPath()
      octx.arc(p.x, p.y + 2, 2, 0, Math.PI * 2)
      octx.fill()
      octx.fillStyle = '#cfe8fa'
      octx.font = '11px system-ui'
      octx.textAlign = 'center'
      octx.fillText('🐟', p.x + lakeR * 1.5, p.y + 6)
      if (spot.id === selectedSpot) {
        octx.strokeStyle = '#ffe066'
        octx.lineWidth = 2
        octx.beginPath()
        octx.ellipse(p.x, p.y, lakeR * 1.6, lakeR * 1.24, 0, 0, Math.PI * 2)
        octx.stroke()
      }
    }

    // ----- Loose items: soft shadow + emblem on a worn disc. -----
    for (const it of hereItems) {
      const p = proj(it.position.x, it.position.y)
      octx.fillStyle = 'rgba(0, 0, 0, 0.25)'
      octx.beginPath()
      octx.ellipse(p.x + 1, p.y + 2.5, 6.5, 3.5, 0, 0, Math.PI * 2)
      octx.fill()
      octx.fillStyle = 'rgba(255, 240, 180, 0.14)'
      octx.beginPath()
      octx.arc(p.x, p.y, 9, 0, Math.PI * 2)
      octx.fill()
      octx.fillStyle = '#fff'
      octx.font = '13px system-ui'
      octx.textAlign = 'center'
      octx.fillText(CATEGORY_ICONS[it.category] ?? '·', p.x, p.y + 4.5)
    }

    // ----- NPCs: shadow, halo, glyph, nameplate. -----
    for (const n of hereNpcs) {
      const p = proj(n.position.x, n.position.y)
      octx.fillStyle = 'rgba(0, 0, 0, 0.28)'
      octx.beginPath()
      octx.ellipse(p.x + 1, p.y + 3, 5.5, 2.6, 0, 0, Math.PI * 2)
      octx.fill()
      if (n.tier === 0) {
        octx.fillStyle = 'rgba(232, 163, 61, 0.2)'
        octx.beginPath()
        octx.arc(p.x, p.y, 11, 0, Math.PI * 2)
        octx.fill()
      }
      octx.fillStyle = 'rgba(20, 24, 32, 0.55)'
      octx.beginPath()
      octx.arc(p.x, p.y, 7.5, 0, Math.PI * 2)
      octx.fill()
      octx.fillStyle = '#fff'
      octx.font = '12px system-ui'
      octx.textAlign = 'center'
      octx.fillText(pickGlyph(n), p.x, p.y + 4.5)
      octx.font = '8.5px system-ui'
      const name = n.name.split(' ')[0]
      const tw = octx.measureText(name).width
      octx.fillStyle = 'rgba(8, 10, 16, 0.6)'
      rr(octx, p.x - tw / 2 - 3, p.y - 15, tw + 6, 10, 3)
      octx.fill()
      octx.fillStyle = '#eee'
      octx.fillText(name, p.x, p.y - 7.5)
      if (n.id === selected) {
        octx.strokeStyle = '#ffe066'
        octx.lineWidth = 2
        octx.beginPath()
        octx.arc(p.x, p.y, 11, 0, Math.PI * 2)
        octx.stroke()
      }
    }

    // ----- Player: lit compass marker. -----
    if (player) {
      const p = proj(player.position.x, player.position.y)
      octx.fillStyle = 'rgba(0, 0, 0, 0.3)'
      octx.beginPath()
      octx.ellipse(p.x + 1, p.y + 3.5, 7, 3.5, 0, 0, Math.PI * 2)
      octx.fill()
      octx.fillStyle = 'rgba(91, 141, 255, 0.28)'
      octx.beginPath()
      octx.arc(p.x, p.y, 13, 0, Math.PI * 2)
      octx.fill()
      octx.fillStyle = '#fff'
      octx.font = '15px system-ui'
      octx.textAlign = 'center'
      octx.fillText(SPRITES.player, p.x, p.y + 5)
      octx.strokeStyle = 'rgba(150, 190, 255, 0.85)'
      octx.lineWidth = 1.5
      octx.beginPath()
      octx.arc(p.x, p.y, 10.5, 0, Math.PI * 2)
      octx.stroke()
    }

    // ----- Atmosphere overlays: fog of war, night tint, weather, vignette. -----
    if (player) {
      const pp = proj(player.position.x, player.position.y)
      const insecurityShrink = 0.05 + (insecurity / 100) * 0.25
      const radius = Math.min(w, h) * (0.52 + 0.26 * dayFactor - insecurityShrink)
      const fw = octx.createRadialGradient(pp.x, pp.y, radius * 0.4, pp.x, pp.y, radius)
      fw.addColorStop(0, 'rgba(0,0,0,0)')
      fw.addColorStop(1, 'rgba(2, 4, 10, 0.5)')
      octx.fillStyle = fw
      octx.fillRect(0, 0, w, h)
    }
    if (dayFactor < 1) {
      const darkness = 1 - dayFactor
      octx.fillStyle = `rgba(8, 12, 40, ${0.36 * darkness})`
      octx.fillRect(0, 0, w, h)
    }
    if (weatherIntensity > 0.05 && dayFactor > 0.3) {
      const murk = seasonTint === SEASON_TINTS.Winter ? '#dce4ec' : '#aebbb0'
      octx.fillStyle = `rgba(${tintRgb(murk)[0]}, ${tintRgb(murk)[1]}, ${tintRgb(murk)[2]}, ${0.1 + weatherIntensity * 0.22})`
      octx.fillRect(0, 0, w, h)
    }
    const vg = octx.createRadialGradient(cx, cy, Math.min(w, h) * 0.45, cx, cy, Math.max(w, h) * 0.8)
    vg.addColorStop(0, 'rgba(0,0,0,0)')
    vg.addColorStop(1, `rgba(2, 3, 8, ${0.16 + (1 - dayFactor) * 0.26})`)
    octx.fillStyle = vg
    octx.fillRect(0, 0, w, h)

    // ----- Living layer: particles + shimmer + hover ring, animated. -----
    const isRain = /rain|storm/i.test(weatherName)
    const isSnow = /snow/i.test(weatherName)
    const isAutumn = seasonName === 'Autumn'
    const isSpring = seasonName === 'Spring'
    const particles: Array<{ x: number; y: number; vx: number; vy: number; ph: number; s: number }> = []
    const count = isRain ? 70 : isSnow ? 36 : isAutumn || isSpring ? 22 : 0
    for (let i = 0; i < count; i++) {
      particles.push({
        x: rng() * w,
        y: rng() * h,
        vx: isRain ? -(1.2 + rng() * 2.2) : (rng() - 0.5) * 0.6,
        vy: isRain ? 4 + rng() * 5 : 0.5 + rng() * 1.1,
        ph: rng() * Math.PI * 2,
        s: 0.6 + rng() * 1.4,
      })
    }
    const fireflies: Array<{ x: number; y: number; ph: number; vx: number; vy: number }> = []
    for (let i = 0; i < 12; i++) {
      fireflies.push({ x: rng() * w, y: rng() * h, ph: rng() * Math.PI * 2, vx: (rng() - 0.5) * 0.25, vy: (rng() - 0.5) * 0.25 })
    }

    let raf = 0
    const tick = (now: number) => {
      ctx.clearRect(0, 0, w, h)
      ctx.drawImage(off, 0, 0, w, h)
      // Rain / snow / drifting petals.
      for (const pt of particles) {
        pt.x += pt.vx
        pt.y += pt.vy
        if (pt.y > h + 8) { pt.y = -8; pt.x = rng() * w }
        if (pt.x < -8) { pt.x = w + 8 }
        if (isRain) {
          ctx.strokeStyle = `rgba(185, 205, 230, ${0.2 + weatherIntensity * 0.25})`
          ctx.lineWidth = 1
          ctx.beginPath()
          ctx.moveTo(pt.x, pt.y)
          ctx.lineTo(pt.x - 5, pt.y + 9)
          ctx.stroke()
        } else if (isSnow) {
          ctx.fillStyle = 'rgba(240, 246, 252, 0.75)'
          ctx.beginPath()
          ctx.arc(pt.x + Math.sin(now / 900 + pt.ph) * 3, pt.y, pt.s, 0, Math.PI * 2)
          ctx.fill()
        } else {
          const sway = Math.sin(now / 900 + pt.ph) * 3
          ctx.fillStyle = isAutumn ? 'rgba(214, 130, 42, 0.85)' : 'rgba(240, 160, 190, 0.8)'
          ctx.beginPath()
          ctx.ellipse(pt.x + sway, pt.y, pt.s * 1.6, pt.s * 0.8, 0.4, 0, Math.PI * 2)
          ctx.fill()
        }
      }
      // Fireflies at night (only when clear).
      if (dayFactor < 0.4 && !isRain && !isSnow) {
        for (const ff of fireflies) {
          ff.x += ff.vx
          ff.y += ff.vy
          if (ff.x < 0) ff.x = w
          if (ff.x > w) ff.x = 0
          if (ff.y < 0) ff.y = h
          if (ff.y > h) ff.y = 0
          const a = 0.25 + 0.5 * (0.5 + 0.5 * Math.sin(now / 320 + ff.ph))
          ctx.fillStyle = `rgba(255, 224, 130, ${a})`
          ctx.beginPath()
          ctx.arc(ff.x, ff.y, 1.6, 0, Math.PI * 2)
          ctx.fill()
        }
      }
// Water shimmer.
      for (const spot of hereSpots) {
        const p = proj(spot.position.x, spot.position.y)
        const r = lakeR
        const t = now / 700
        for (let k = 0; k < 3; k++) {
          const a = t * 0.9 + (k / 3) * Math.PI * 2
          const dx = Math.cos(a) * r * 0.4
          const dy = Math.sin(a) * r * 0.28
          const shimmer = 0.3 + 0.5 * (0.5 + 0.5 * Math.sin(now / 240 + k * 2.4))
          ctx.fillStyle = `rgba(220, 240, 255, ${shimmer})`
          ctx.beginPath()
          ctx.arc(p.x + dx, p.y + dy, 1.3, 0, Math.PI * 2)
          ctx.fill()
        }
      }
      // Hover ring.
      if (hoverRef.current) {
        const hr = hoverRef.current
        ctx.strokeStyle = hr.color
        ctx.lineWidth = 2
        ctx.setLineDash([4, 4])
        ctx.beginPath()
        ctx.arc(hr.x, hr.y, 12, 0, Math.PI * 2)
        ctx.stroke()
        ctx.setLineDash([])
      }
      raf = requestAnimationFrame(tick)
    }
    raf = requestAnimationFrame(tick)
    return () => cancelAnimationFrame(raf)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [state, selected, selectedBuilding, selectedItem, selectedPlot, selectedSpot, selectedJob, zoom, cam])

  const hoverTarget = (wxw: number, wyw: number): { x: number; y: number; color: string; label: string } | null => {
    const c = canvasRef.current
    const s = ((c?.clientHeight ?? 600) * zoom) / baseView
    let best: { x: number; y: number; color: string; label: string } | null = null
    let bestD = Infinity
    const consider = (x: number, y: number, radius: number, color: string, label: string) => {
      const d = Math.hypot(x - wxw, y - wyw)
      if (d < radius && d < bestD) {
        bestD = d
        best = { x, y, color, label }
      }
    }
    for (const n of hereNpcs) consider(n.position.x, n.position.y, 30 / s, '#ffe066', `${n.name} ${n.surname} — ${n.occupation}`)
    for (const b of hereBuildings) {
      const bcx = (b.bounds.min_x + b.bounds.max_x) / 2
      const bcy = (b.bounds.min_y + b.bounds.max_y) / 2
      if (wxw >= b.bounds.min_x && wxw <= b.bounds.max_x && wyw >= b.bounds.min_y && wyw <= b.bounds.max_y) {
        consider(bcx, bcy, 40 / s, '#ffe066', b.name)
      }
    }
    for (const it of hereItems) consider(it.position.x, it.position.y, 18 / s, '#ffd97a', it.name)
    for (const pl of herePlots) consider(pl.position.x, pl.position.y, 14 / s, '#ffe066', `Crop plot (stage ${pl.stage}) — water ${Math.round(pl.water_level * 100)}%`)
    for (const sp of hereSpots) consider(sp.position.x, sp.position.y, 22 / s, '#7fd4ff', sp.name)
    for (const j of hereJobs) consider(j.work_position.x, j.work_position.y, 18 / s, '#ffd97a', j.title)
    return best
  }

  const handleMouseMove = (e: React.MouseEvent<HTMLCanvasElement>) => {
    if (dragRef.current) return
    const c = canvasRef.current
    if (!c) return
    const rect = c.getBoundingClientRect()
    const x = e.clientX - rect.left
    const y = e.clientY - rect.top
    const w2 = c.clientWidth
    const h2 = c.clientHeight
    const s = (h2 * zoom) / baseView
    const wxw = (x - w2 / 2) / s + cam.x
    const wyw = (y - h2 / 2) / s + cam.y
    const t = hoverTarget(wxw, wyw)
    setHover(t?.label ?? null)
    hoverRef.current = t ? { x: t.x, y: t.y, color: t.color } : null
  }

  const handlePointerDown = (e: React.PointerEvent<HTMLCanvasElement>) => {
    if (e.button !== 0) return
    e.currentTarget.setPointerCapture(e.pointerId)
    dragRef.current = { px: e.clientX, py: e.clientY, cx: cam.x, cy: cam.y, moved: false }
    dragMovedRef.current = false
    setDragging(true)
  }
  const handlePointerMove = (e: React.PointerEvent<HTMLCanvasElement>) => {
    const d = dragRef.current
    if (!d) return
    const c = canvasRef.current
    if (!c) return
    const h2 = c.clientHeight
    const s = (h2 * zoom) / baseView
    const nx = d.cx - (e.clientX - d.px) / s
    const ny = d.cy - (e.clientY - d.py) / s
    if (Math.hypot(e.clientX - d.px, e.clientY - d.py) > 4) {
      d.moved = true
      dragMovedRef.current = true
    }
    setCam(clamped(nx, ny))
  }
  const handlePointerUp = (e: React.PointerEvent<HTMLCanvasElement>) => {
    if (dragRef.current) e.currentTarget.releasePointerCapture(e.pointerId)
    dragRef.current = null
    setDragging(false)
  }

  const clamped = (nx: number, ny: number) => clampCam(nx, ny)

  const handleClick = (e: React.MouseEvent<HTMLCanvasElement>) => {
    if (dragMovedRef.current) return
    const c = canvasRef.current
    if (!c) return
    const rect = c.getBoundingClientRect()
    const x = e.clientX - rect.left
    const y = e.clientY - rect.top
    const w2 = c.clientWidth
    const h2 = c.clientHeight
    const s = (h2 * zoom) / baseView
    const wxw = (x - w2 / 2) / s + cam.x
    const wyw = (y - h2 / 2) / s + cam.y

    // Nearest NPC within click radius
    let best: NPC | null = null
    let bestDist = 30 / s
    for (const n of hereNpcs) {
      const d = Math.hypot(n.position.x - wxw, n.position.y - wyw)
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
      if (wxw >= b.bounds.min_x && wxw <= b.bounds.max_x && wyw >= b.bounds.min_y && wyw <= b.bounds.max_y) {
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
    let iBest: Item | null = null
    let bestDistItem = 20 / s
    for (const it of hereItems) {
      const d = Math.hypot(it.position.x - wxw, it.position.y - wyw)
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
    let bestDistPlot = 18 / s
    for (const pl of herePlots) {
      const d = Math.hypot(pl.position.x - wxw, pl.position.y - wyw)
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
    let bestDistSpot = 30 / s
    for (const sp of hereSpots) {
      const d = Math.hypot(sp.position.x - wxw, sp.position.y - wyw)
      if (d < bestDistSpot) {
        bestDistSpot = d
        sBest = sp
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
    let bestDistJob = 25 / s
    for (const j of hereJobs) {
      const d = Math.hypot(j.work_position.x - wxw, j.work_position.y - wyw)
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
    onMove(wxw, wyw)
    setSelected(null)
    setHover(null)
  }

  const handleWheel = (e: React.WheelEvent<HTMLCanvasElement>) => {
    setZoom((z) => Math.min(2.6, Math.max(0.55, z * (e.deltaY < 0 ? 1.15 : 1 / 1.15))))
  }

  const selectedNpc = hereNpcs.find((n) => n.id === selected) ?? null
  const selectedItemObj = hereItems.find((i) => i.id === selectedItem) ?? null

  const hour = state.time_data?.hour ?? 12
  const minute = state.time_data?.minute ?? 0
  const hh = String(hour).padStart(2, '0')
  const mm = String(minute).padStart(2, '0')

  return (
    <div className="panel map-panel">
      <h3>
        <span className="map-region">🗺️ {region?.name ?? 'Unknown region'}</span>
        <span className="map-chips">
          <span className="map-chip">{hour >= 19 || hour < 6 ? '🌙' : '☀️'} {hh}:{mm}</span>
          <span className="map-chip">{weatherIcon(state.time_data?.weather)} {state.time_data?.season ?? ''}</span>
          <span className="map-chip">{weatherIcon(state.time_data?.weather)} {state.time_data?.weather ?? ''}</span>
        </span>
      </h3>
      <div className="map-canvas-wrap">
        <canvas
          ref={canvasRef}
          className={`map-canvas ${dragging ? 'map-dragging' : ''}`}
          onClick={handleClick}
          onMouseMove={handleMouseMove}
          onMouseLeave={() => { setHover(null); hoverRef.current = null }}
          onWheel={handleWheel}
          onPointerDown={handlePointerDown}
          onPointerMove={handlePointerMove}
          onPointerUp={handlePointerUp}
          onPointerCancel={handlePointerUp}
        />
        <div className="map-zoom">
          <button onClick={() => setZoom((z) => Math.max(0.55, z / 1.2))} title="Zoom out">−</button>
          <span>{Math.round(zoom * 100)}%</span>
          <button onClick={() => setZoom((z) => Math.min(2.6, z * 1.2))} title="Zoom in">+</button>
          <button onClick={() => { setZoom(1); if (player) setCam({ x: player.position.x, y: player.position.y }) }} title="Re-center on player">◎</button>
        </div>
      </div>
      {hover && <div className="map-hover">{hover}</div>}
      {dragging && <div className="map-hover map-hover-drag">drag to pan</div>}

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