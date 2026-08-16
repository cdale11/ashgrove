# Ashgrove Valley — Design Decisions Log
## Recorded 2026-08-16 from User Collaboration

---

## P0: Authored Town Design

### Districts (8)
1. **Civic** — Town Hall, services, governance
2. **Commercial** — Shops, market, trade
3. **Residential** — NPC homes, inns
4. **Industrial** — Workshops, processing, storage
5. **Riverside** — Docks, fishing, fog zones (horror)
6. **Woodland** — Whisper Wood edge, Witch's hut
7. **Farmstead** — Player farm, NPC farms, communal fields
8. **Horror** — Abandoned sanitarium, ritual sites, basement access

### Key Horror Locations (5 Anchors)
| Location | District | Narrative Role |
|----------|----------|----------------|
| Basement entrance | Civic (Town Hall cellar) | Central mystery access; only after midnight |
| Witch's hut | Woodland | Knowledge broker, cycle witness |
| Abandoned sanitarium | Horror | Past trauma site, collective guilt manifestation |
| Ritual circle | Forest (Woodland/Forest border) | Cycle mechanics, entity communication |
| Fog zones | Riverside (night) | Perception filter, entity manifestation |

### Important NPCs (Full Cognition — 7)
| NPC | Role | District | Secret / Horror Connection |
|-----|------|----------|---------------------------|
| **Mayor** | Governance, order | Civic | Knows the cycle; suppresses truth to maintain control |
| **Witch** | Knowledge, magic | Woodland | Sees cycles; guides/manipulates player |
| **Traveler** | Outsider, observer | Commercial (inn) | Not bound by cycle; remembers across loops |
| **Doctor** | Health, sanity | Civic/Residential | Complicit; treats symptoms not cause |
| **Teacher** | Education, history | Civic | Witness; records but cannot act |
| **Carpenter** | Building, repair | Industrial | Maintains physical barriers; knows structural weaknesses |
| **Farmer** | Food, land steward | Farmstead | Connection to Valley's body; notices ecological shifts |

**Background NPCs**: Cheaper cognition (statistical/behavioral only)

---

## P1: Horror Narrative Design

### Core Concept: The Valley Itself (Genius Loci)
- **The land is alive/cursed** — the valley itself is the entity
- **Collective guilt awakened it** — historical sins (witch trials, betrayal, massacre) gave it consciousness
- **Three intertwined mechanisms**:
  1. **Time loops / cyclical tragedy** — events repeat with variations; truth hidden in each cycle
  2. **Entity feeding on fear/sanity** — the Valley consumes sanity/fear to grow stronger
  3. **Collective guilt manifests physically** — corruption, fog, mutations are guilt made manifest

### Cycle Structure (Higurashi-Inspired)
- **Number of loops**: 4-5 major cycles before "true ending" path unlocks
- **Escalation**: Each cycle reveals more truth; horror intensity increases; NPC behaviors diverge
- **Divergence points**: Player choices in each cycle affect next cycle's starting state
- **Memory across cycles**: Traveler remembers; Witch remembers; Valley remembers; player retains knowledge

### Sanity / Perception Filters
- **Hallucinations**: Visual/auditory false perceptions (PIXI client effects)
- **Distorted dialogue**: NPC text corrupted by sanity level
- **False UI**: Inventory/map shows wrong info at low sanity
- **Meta-narrative breaks** (DDLC-style): Fourth-wall moments when sanity critical
- **Internal voices** (Disco Elysium-style): Skills/personality facets comment on situation

### Basement / Under-Map
- Accessible only after midnight (in-game 24:00-04:00)
- Procedural horror content generated per cycle
- Persistent consequences: changes here affect surface world
- Valley's "heart" — corruption source

---

## P2: Recurring Runs / Cross-Run Persistence

### Run Model: Single Persistent World (Death = Penalties, Not Reset)
- **Everything persists**: World state, NPC relationships, buildings, map discoveries, crafted items, knowledge
- **Death penalties**: HP/position reset; sanity damage; possible item loss; time loss; NPC relationship damage
- **No run counter / no full reset**: Single continuous world
- **Knowledge progression**: Player learns horror truth, Valley secrets, optimal strategies — this is the meta-progression
- **Death matters narratively**: Each death is a "loop" in the Valley's cycle; NPCs may reference past deaths

### What Resets on Death
- Player HP → full
- Player position → last safe point (bed, town hall)
- Sanity → reduced (not zero)
- Temporary buffs/debuffs cleared

### What Persists
- All world state (terrain, buildings, NPCs, economy, ecology)
- NPC memories/relationships (they remember player's deaths)
- Player knowledge (map, recipes, horror truths, NPC secrets)
- Crafted items in storage
- Building progress

---

## Cognitive Architecture Priorities

### Important NPCs (Full CognitiveCore) — 7
1. Mayor
2. Witch
3. Traveler
4. Doctor
5. Teacher
6. Carpenter
7. Farmer

### Background NPCs (Cheaper Cognition)
- Statistical behavior only
- No episodic memory, no social graph, no self-model
- Simple drive-based action selection

### Aggregate Minds Implementation Order
1. **NatureMind** (Forest/Ecology) — connects to Valley entity, horror fog, ecological succession
2. **VillageMind** (NPC Collective) — emotional states, cultural practices, schedule bias
3. **EconomyMind** — commodity cycles, player impact, market dynamics
4. **CultureMind** — shared beliefs, rituals, collective fears
5. **PerformanceMind** — already partially implemented

---

## Next Implementation Steps

1. **Wire CognitiveCore into NPC tick** — game loop integration
2. **Generate MLP training data via NIM** — attention, action_evaluator, world_model
3. **Build Horror Location structures** — basement, witch hut, sanitarium, ritual circle, fog zones
4. **Implement NatureMind** — forest ecology aggregate
5. **Implement Valley Entity mechanics** — collective guilt → corruption → horror intensity

---

*Recorded 2026-08-16. Update as decisions evolve.*