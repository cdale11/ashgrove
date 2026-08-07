# ashgrove.md

# Project Name

**Ashgrove**

---

# Vision Statement

Build a single-player psychological horror RPG where the player investigates an isolated rural village called **Ashgrove** that appears ordinary on the surface but gradually reveals disturbing truths.

The world is not a backdrop—it is a living simulation.

Villagers possess memories, personalities, relationships, beliefs, routines, ambitions, fears, and the ability to change over time. The player is never the center of the universe; life continues around them while they participate in it.

The horror emerges from both authored narrative and autonomous simulation.

---

# Core Design Pillars

## 1. Living World

The world always behaves like a real place.

NPCs have:

* Jobs
* Families
* Relationships
* Daily routines
* Goals
* Beliefs
* Memories
* Emotions

Villages evolve naturally.

Buildings appear.

Businesses open and close.

People marry.

Children grow.

Elders die.

Weather changes.

Seasons matter.

The world exists independently of the player's wishes.

---

## 2. Investigation

The player is an investigator.

However, investigation extends far beyond solving crimes.

The player investigates:

* Murders
* Missing people
* Rumors
* Folklore
* Family histories
* Contradictory memories
* Relationships
* Ancient records
* Supernatural phenomena
* Their own assumptions

Knowledge is one of the strongest forms of progression.

---

## 3. Psychological Horror

The horror relies primarily on psychological tension rather than combat or jump scares.

Themes include:

* Paranoia
* Unreliable memories
* Isolation
* Manipulation
* False beliefs
* Emotional attachment
* Conflicting realities
* Existential dread

The village slowly changes from comforting to disturbing.

The player should question whether events are supernatural, psychological, or socially constructed.

---

## 4. Emergence

Whenever possible, systems should produce believable outcomes instead of scripted ones.

Examples:

* Friendships
* Rivalries
* Love
* Hatred
* Crimes
* Rumors
* Traditions
* Festivals
* Economic changes
* Family histories

Only major narrative milestones are authored.

Everything else should emerge from the simulation.

---

# High-Level Gameplay

The player is free to choose how to spend each day.

Possible activities include:

* Investigation
* Side quests
* Main quests
* Fishing
* Hunting
* Foraging
* Crafting
* Cooking
* Trading
* Running businesses
* Exploring forests
* Exploring caves
* Wildlife observation
* Farming
* Gathering evidence
* Building relationships
* Learning village history
* Participating in festivals
* Helping villagers
* Discovering secrets

There is no mandatory daily routine.

The world presents opportunities.

The player chooses.

---

# Gameplay Progression

Several progression systems exist simultaneously.

## Character Progression

* Levels
* Experience
* Attributes
* Skills
* Professions
* Reputation
* Wealth
* Equipment
* Investigation ability
* Social ability

Combat exists but is intentionally not the dominant progression system.

---

## Knowledge Progression

The player gradually learns:

* Village history
* Hidden locations
* Family secrets
* Rituals
* Folklore
* Relationships
* Mysteries
* Hidden mechanics

Knowledge permanently changes how future playthroughs are approached.

---

## World Progression

The village evolves.

Businesses appear.

Buildings decay.

Families expand.

Children become adults.

Economies fluctuate.

Politics change.

The world slowly remembers its own history.

---

## Horror Progression

The horror develops gradually.

The intended pacing is:

Arrival

↓

Comfort

↓

Unease

↓

Suspicion

↓

Paranoia

↓

Collapse

↓

Truth

↓

Repeat

The first hours should feel almost peaceful.

The horror infects ordinary life rather than replacing it.

---

# Replayability

The core story remains largely fixed.

However, every playthrough differs through:

* Village layout variations
* Forest generation
* Cave generation
* NPC relationships
* Emergent events
* Side quests
* Wildlife
* Weather
* Economic conditions
* Daily routines
* Discoveries
* Hidden content

Some narrative layers require multiple playthroughs to fully understand, inspired by—but not copying—the structure of games like DDLC.

---

# World

## Scale

The playable world consists of:

* One handcrafted village: **Ashgrove**
* Surrounding forests
* Rivers
* Lakes
* Mountains
* Caves
* Nearby landmarks

The central village is handcrafted.

Surrounding regions combine handcrafted locations with procedural expansion.

---

## Travel

Players travel using:

* Walking
* Bicycle
* Bus
* Train

Additional transportation may unlock later.

---

## Seasons

The game contains:

* Spring
* Summer
* Autumn
* Winter

Seasons significantly affect:

* Crops
* Wildlife
* Festivals
* NPC routines
* Weather
* Resources
* Economy
* Clothing
* Exploration

---

# Survival

Survival mechanics exist but remain lightweight.

Systems include:

* Hunger
* Fatigue
* Temperature
* Minor illness
* Injuries

These mechanics support immersion without overwhelming investigation and narrative.

---

# Economy

The player may:

* Own houses
* Rent rooms
* Purchase land
* Farm
* Fish
* Trade
* Operate businesses
* Invest
* Hire workers

The goal is financial freedom, not empire management.

The economy is simulated and responsive to world events.

---

# NPC Design

NPCs are divided into cognitive tiers.

## Tier 1 — Major Characters

Approximately 4–8 persistent, deeply simulated characters.

Each possesses:

* Rich autobiographical memory
* Long-term goals
* Internal reflection
* Emotional model
* Dynamic personality
* Belief system
* Relationship graph
* Local LLM support
* Ability to lie
* Ability to misunderstand
* Ability to learn
* Ability to change

These characters carry the emotional core of the story.

---

## Tier 2 — Persistent Villagers

Dozens of villagers with:

* Jobs
* Families
* Memories
* Relationships
* Personalities
* Emotional state
* Daily schedules

LLM reasoning is invoked selectively rather than continuously.

---

## Tier 3 — Background Population

Simplified citizens using lightweight behavioral simulation.

Purpose:

* Populate the world
* Support the economy
* Increase realism
* Consume minimal resources

---

# NPC Capabilities

NPCs may:

* Remember
* Forget
* Misremember
* Lie
* Gossip
* Fall in love
* Hate
* Fear
* Forgive
* Change beliefs
* Develop personalities
* Invent traditions
* Create rumors
* Raise families
* Teach children
* Learn from experience

No NPC exists solely for quests.

Every NPC has an independent life.

---

# Time System

Time advances only while the player is actively playing.

Every meaningful player action advances simulation time.

When the player exits the game:

Simulation pauses.

Nothing progresses while offline.

This ensures:

* Fairness
* Narrative pacing
* Predictability
* No fear of missing important events

---

# Story Premise

The player arrives in **Ashgrove**, an isolated mountain village, to investigate a long-forgotten disappearance connected to local folklore.

Initially, everything appears peaceful.

Over time, contradictions emerge.

Historical records disagree.

People remember impossible events.

Photographs change.

Rumors become accepted facts.

The deeper the player investigates, the harder it becomes to distinguish memory, belief, and reality.

The story explores how shared belief can reshape both society and the player's understanding of truth.

---

# Narrative Philosophy

The game is inspired by the emotional and structural qualities of psychological horror works such as *Higurashi* and *DDLC*, but it tells an original story with its own setting, characters, mysteries, and themes.

The goal is not imitation but the creation of a distinct identity.

---

# AI Philosophy

Artificial Intelligence exists to support believable cognition.

The simulation always remains authoritative.

LLMs never directly modify world state.

Instead:

NPC thinks

↓

LLM proposes

↓

Simulation validates

↓

World updates

The simulation decides what is actually possible.

---

# Technical Philosophy

The project follows a server-client architecture.

## Backend

C++20/23

Responsibilities:

* World simulation
* Time
* NPCs
* Quests
* Economy
* Combat
* Weather
* Procedural generation
* Save system
* Networking

---

## AI

llama.cpp

Local GGUF models

Future ML integration using Python where appropriate.

Python is used for:

* Machine learning
* Offline tools
* Dataset generation
* Asset processing
* Research

Python is not the authoritative simulation runtime.

---

## Frontend

Web-based interface.

Possible stack:

* React
* TypeScript

Communication:

* REST
* WebSockets

The browser is a visualization client.

The C++ server owns the world.

---

# Guiding Principles

1. The world never exists for the player's convenience.

2. Every important event has a believable cause.

3. NPCs have agency.

4. The simulation is the source of truth.

5. Emergence is preferred over scripting whenever practical.

6. Knowledge is progression.

7. Ordinary life makes horror meaningful.

8. Computational resources are concentrated where they matter most.

9. Systems should scale to approximately 8 GB RAM.

10. Every feature should strengthen the feeling that **Ashgrove** is alive.

---

# Long-Term Vision

The project aims to become a platform for emergent storytelling rather than a conventional scripted RPG.

The ultimate objective is to create a world where players remember not only the story they experienced, but the lives they witnessed, the relationships they formed, and the mysteries that emerged uniquely from their own playthrough.

