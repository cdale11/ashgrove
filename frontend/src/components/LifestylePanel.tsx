import type { WorldState, CropPlot, FishingSpot, JobPosting, ResourceDeposit, CraftRecipe } from '../types'

interface LifestylePanelProps {
  state: WorldState
  onPlant: (plotId: number, crop: string) => void
  onWater: (plotId: number) => void
  onHarvest: (plotId: number) => void
  onFish: (spotId: number) => void
  onWork: (jobId: number) => void
  onGather: (depositId: number) => void
  onExpandFarm: () => void
  onCraft: (recipeKey: string) => void
}

const CROP_NAMES: Record<number, string> = {
  0: '—',
  1: 'Wheat',
  2: 'Carrots',
  3: 'Potatoes',
  4: 'Cabbage',
  5: 'Herbs',
  6: 'Flax',
  7: 'Turnips',
}

const STAGE_NAMES: Record<number, string> = {
  0: 'Empty',
  1: 'Planted',
  2: 'Sprouting',
  3: 'Growing',
  4: 'Flowering',
  5: 'Ready',
  6: 'Withered',
}

const JOB_NAMES: Record<number, string> = {
  0: '—',
  1: 'Farmhand',
  2: 'Fisher',
  3: 'Woodcutter',
  4: 'Miner',
  5: 'Blacksmith',
  6: 'Inn Helper',
  7: 'Guard',
  8: 'Courier',
  9: 'Herbalist',
}

const SKILL_NAMES: Record<number, string> = {
  1: 'farming',
  2: 'fishing',
  3: 'woodcutting',
  4: 'mining',
  5: 'smithing',
  6: 'trading',
  7: 'combat',
  8: 'stealth',
  9: 'herbalism',
}

export function LifestylePanel({ state, onPlant, onWater, onHarvest, onFish, onWork, onGather, onExpandFarm, onCraft }: LifestylePanelProps) {
  const player = state.player
  const playerRegion = player?.region_id ?? state.world.regions[0]?.id ?? 0

  const plots = state.world.crop_plots.filter((p) => p.position.region_id === playerRegion)
  const spots = state.world.fishing_spots.filter((s) => s.position.region_id === playerRegion)
  const jobs = state.world.job_postings.filter((j) => j.region_id === playerRegion && j.is_active)
  const deposits = state.world.resource_deposits.filter((d) => d.position.region_id === playerRegion)
  const recipes = state.recipes ?? []

  const hasIngredients = (r: CraftRecipe) => {
    if (!player) return false
    return r.costs.every((c) => {
      const owned = player.inventory.filter((id) =>
        state.world.items.find((it) => it.id === id && it.name === c.name),
      ).length
      return owned >= c.count
    })
  }

  const skillLabel = (jobType: number): [string, number] => {
    const name = SKILL_NAMES[jobType]
    return [name ?? 'skill', player ? (player.skills[name] ?? 0) : 0]
  }

  const skillSummary = () => {
    if (!player) return ''
    const top = Object.entries(player.skills)
      .sort((a, b) => b[1] - a[1])
      .slice(0, 2)
      .map(([k, v]) => `${k}: ${v.toFixed(0)}`)
      .join(' · ')
    return top ? `(top skills: ${top})` : ''
  }

  return (
    <div className="panel lifestyle-panel">
      <h3>🌾 Daily Life: {skillSummary()}</h3>

      {plots.length > 0 && (
        <div className="life-section">
          <h4>Crop Plots ({plots.length})</h4>
          {plots.map((p: CropPlot) => (
            <div key={p.id} className="life-entry">
              <span className="life-name">{CROP_NAMES[p.crop] ?? 'Unknown'} — {STAGE_NAMES[p.stage] ?? p.stage}</span>
              <span className="life-meta">
                🌱 {Math.round(p.progress * 100)}% · 💧 {Math.round(p.water_level * 100)}%
              </span>
              <div className="life-actions">
                {p.stage === 0 && <button onClick={() => onPlant(p.id, 'wheat')}>Plant Wheat</button>}
                {p.stage === 0 && <button onClick={() => onPlant(p.id, 'carrots')}>Plant Carrots</button>}
                {p.stage === 0 && <button onClick={() => onPlant(p.id, 'potatoes')}>Plant Potatoes</button>}
                {p.stage !== 0 && p.stage !== 6 && p.water_level < 1 && <button onClick={() => onWater(p.id)}>Water</button>}
                {p.stage === 5 && <button className="harvest" onClick={() => onHarvest(p.id)}>Harvest</button>}
              </div>
            </div>
          ))}
        </div>
      )}

      {spots.length > 0 && (
        <div className="life-section">
          <h4>Fishing Spots ({spots.length})</h4>
          {spots.map((s: FishingSpot) => (
            <div key={s.id} className="life-entry">
              <span className="life-name">{s.name}</span>
              <span className="life-meta">
                🐟 {s.fish_density.toFixed(1)} · Difficulty {s.difficulty.toFixed(1)}
              </span>
              <div className="life-actions">
                <button className="fish" onClick={() => onFish(s.id)}>Fish</button>
              </div>
            </div>
          ))}
        </div>
      )}

      {jobs.length > 0 && (
        <div className="life-section">
          <h4>Work ({jobs.length})</h4>
          {jobs.map((j: JobPosting) => {
            const [skName, skVal] = skillLabel(j.type)
            return (
              <div key={j.id} className="life-entry">
                <span className="life-name">{j.title}</span>
                <span className="life-meta">
                  {JOB_NAMES[j.type] ?? j.type} · {j.wage_per_hour.toFixed(1)}💰/hr × {j.hours_per_shift.toFixed(0)}hr · {skName} {skVal.toFixed(0)}
                </span>
                <div className="life-actions">
                  <button className="work" onClick={() => onWork(j.id)}>Work shift</button>
                </div>
              </div>
            )
          })}
        </div>
      )}

      {deposits.length > 0 && (
        <div className="life-section">
          <h4>Resources ({deposits.length})</h4>
          {deposits.map((d: ResourceDeposit) => (
            <div key={d.id} className="life-entry">
              <span className="life-name">{d.resource_name}</span>
              <span className="life-meta">
                {Math.round(d.amount)}/{Math.round(d.max_amount)}{d.depleted ? ' · depleted' : ''}
              </span>
              <div className="life-actions">
                <button className="gather" disabled={d.depleted || d.amount <= 0} onClick={() => onGather(d.id)}>
                  Gather
                </button>
              </div>
            </div>
          ))}
        </div>
      )}

      {plots.length > 0 && (
        <div className="life-section">
          <h4>Farm</h4>
          <div className="life-actions">
            <button className="expand" onClick={() => onExpandFarm()}>Clear new plot</button>
          </div>
        </div>
      )}

      {recipes.length > 0 && (
        <div className="life-section">
          <h4>Craft ({recipes.length})</h4>
          {recipes.map((r: CraftRecipe) => (
            <div key={r.key} className="life-entry">
              <span className="life-name">{r.name}</span>
              <span className="life-meta">
                {r.costs.map((c) => `${c.count}× ${c.name}`).join(', ')} · {r.value}💰
                {r.skill_req > 0 && ` · ${r.skill} ${r.skill_req}`}
              </span>
              <div className="life-actions">
                <button className="craft" disabled={!hasOwn(r)} onClick={() => onCraft(r.key)}>Craft</button>
              </div>
            </div>
          ))}
        </div>
      )}

      {plots.length === 0 && spots.length === 0 && jobs.length === 0 && deposits.length === 0 && (
        <p className="empty-state">No farms, waters, or work in this region yet.</p>
      )}
    </div>
  )
}