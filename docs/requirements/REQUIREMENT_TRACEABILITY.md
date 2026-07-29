# Requirement traceability matrix

Version: G0.1  
Source: `GOV-001` in `docs/governance/GOVERNING_DOCUMENTS.md`

This matrix is the navigation and allocation view. The authoritative atomic
source extraction is split between `ATOMIC_REQUIREMENT_CATALOG.md` (all 1,044
bullet/numbered-list items) and `NORMATIVE_PROSE_CATALOG.md` (165 complete prose
paragraphs). Both preserve source lines and exact normalized wording. Feature
ranges below directly name their corresponding atomic catalog IDs. Evidence paths are planned authoritative
locations, not claims of completion.

## Product and cross-cutting requirements

| Requirement ID | Requirement | Planned authoritative evidence |
|---|---|---|
| IP-001..015 | No copied code, proprietary data/assets/layouts/glyphs/names/lore/text/events/implementation details/formats or reverse-engineered assets; independently designed ASCII | license audit, provenance records, UI review |
| PROD-001..018 | Deterministic generator; C tile core; linked strategic simulation; C++ orchestration; terminal client; replay; scripted AI; PPO; league; human-agent play; browser; benchmarks; saves/replays; scenarios; mods; stable APIs; reproducible artifacts; 50 updates | update reports and final manifest |
| ARCH-TILE-* | Tile representation, authoritative responsibilities, and consistency with region graph | architecture tests and U01/U39 reports |
| ARCH-REGION-* | Strategic graph representation, authoritative responsibilities, and physical/political edge validity | architecture tests and U01/U39 reports |
| CORE-* | C owns all listed authoritative state/transitions and satisfies C standard, ownership, storage, error, input, determinism, opaque API, fuzz, and reference-mode rules | ADRs, C API, build/test/fuzz evidence |
| CPP-* | C++ owns orchestration/ML/clients without alternate rules; pinned standard, RAII, boundary, thread, warning, analysis, sanitizer, measured-abstraction rules | ADRs and C++ build/test reports |
| COMPUTE-* | CPU-first environments; batched C++; GPU learner/inference; CPU fallback; explicit PPO backend decisions; measured custom-kernel gate | performance plan and U47 report |
| WORLD-* | Version/config/seed/content reproducibility; bounded structured generation; all listed geography/history outputs and validity/fairness/tie/cross-platform constraints | U01, U39, U42, U45 evidence |
| HISTORY-* | Bounded causal prehistory processes affect live diplomatic, demographic, economic, infrastructure, language, military, attitude, and character state | U42 evidence |
| ASCII-* | Independent terminal rendering, legend/fallback/color/headless/viewports/modes/inspectors and all 18 named screens | ASCII spec and visible-update reports |
| TIME-* | Deterministic time scales, explicit 23-stage order, no unordered-iteration or timing dependence | determinism spec and step-order tests |
| NUM-* | Integer/fixed-point authoritative values, controlled floating point, overflow checks | numeric ADR and sanitizer/property tests |
| ETHICS-* | Material consequences; local political/economic agency; no assumed foreign superiority or inevitability | mechanics specs, scenario/property tests, review |
| ACTION-* | Hierarchical categories/parameters, exact masks, independent validation, transactional structured invalid-action behavior and diagnostics | C API/action specs and tests |
| OBS-* | Segmented partial observations with complete feature metadata and mandatory leakage tests | observation schema and U47 tests |
| RL-* | Masked hierarchical PPO, GAE/value/entropy/recurrent/batching/attribution/league/checkpoint/evaluation/statistics plus 11 validation environments | U47/U48 evidence |
| TEAM-* | Queen/Warden separation, specialized workers, independent critical-code review | review records and evidence provenance |
| EVID-* | Every update uses the mandatory evidence-report fields; only Warden assigns VERIFIED | 50 evidence reports and registry validation |
| PROHIB-001..040 | All 40 numbered strict prohibitions | automated policy checks plus Warden audit |
| REGISTRY-* | Registry exists, uses allowed states, Warden-only verification, regression reopening | update registry and change history |
| DEVSEQ-* | 17 preproduction Gate Zero actions, approval gate, then automatic earliest dependency-valid work | Gate Zero report and dependency graph |
| PERF-* | Every required measure and every condition for valid claims; optimizations preserve rules | performance plan and raw benchmark artifacts |
| FINAL-* | Complete final campaign, two named release artifacts, exactly 50 VERIFIED rows | final evidence manifest/report |

## Mandatory update requirements

| IDs | Update and complete feature summary | Planned evidence |
|---|---|---|
| FD-UPD-01-F001..F009 | Procedural Coastal Corridor: coastal/inland settlements, river, road candidate, terrain, two factions, local polity, ASCII, deterministic generation | `docs/evidence/updates/U01.md` |
| FD-UPD-02-F001..F010 | Expedition Personnel: leader, doctor, engineer, quartermaster, guides, porters, health, morale, skills and named histories | `U02.md` |
| FD-UPD-03-F001..F009 | Expedition Inventory: food, medicine, tools, trade goods, tents, spares, capacity, consumption, loss/spoilage | `U03.md` |
| FD-UPD-04-F001..F007 | Camps and Rest: camps, rest, shelter, quality, recovery, security, routines | `U04.md` |
| FD-UPD-05-F001..F007 | Exploration/Fog: unknowns, survey, scout, confidence, decay, hidden terrain, discovery records | `U05.md` |
| FD-UPD-06-F001..F008 | Terrain Movement: forest, grassland, hills, mountains, wetlands, desert, rivers, roads | `U06.md` |
| FD-UPD-07-F001..F008 | Weather/Seasons: rain, heat, dry/wet seasons, flood, travel/demand/visibility effects | `U07.md` |
| FD-UPD-08-F001..F009 | Disease: exposure, infection, incubation, symptoms, treatment, recovery, mortality, disease risks, hospital support | `U08.md` |
| FD-UPD-09-F001..F009 | Local Polity: government, settlements, leaders, internal factions, treasury, military, trade policy, relations, own objectives | `U09.md` |
| FD-UPD-10-F001..F009 | Diplomacy/Access: requests, trade, transit, gifts, negotiation, duration, breach, trust, legitimacy | `U10.md` |
| FD-UPD-11-F001..F008 | Trade/Markets: prices, supply, demand, buy/sell, saturation, history, revenue | `U11.md` |
| FD-UPD-12-F001..F008 | Porter/Caravan: teams, pack animals, wagons, capacity, fatigue, routes, convoy movement, losses | `U12.md` |
| FD-UPD-13-F001..F008 | River Transport: canoes, barges, riverboats, docks, navigability, currents, loading, routes | `U13.md` |
| FD-UPD-14-F001..F008 | Roads: survey, construction, grades, bridges, maintenance, ownership, bonuses, cost | `U14.md` |
| FD-UPD-15-F001..F007 | Supply Depots: construction, storage, resupply, spoilage, security, capacity, range | `U15.md` |
| FD-UPD-16-F001..F009 | Passenger Transport: traveler classes, fares, demand, satisfaction, destinations | `U16.md` |
| FD-UPD-17-F001..F007 | Cargo Contracts: generation, deadlines, volumes, eligibility, payment, penalties, reliability | `U17.md` |
| FD-UPD-18-F001..F009 | Settlement Growth: population, housing, food, trade access, health, migration, growth/decline, services | `U18.md` |
| FD-UPD-19-F001..F008 | Agriculture: farms, crops, seasons, yields, labor, storage, famine, market supply | `U19.md` |
| FD-UPD-20-F001..F008 | Extractive industry: mines, quarries, forestry, deposits, production, labor, transport, environment | `U20.md` |
| FD-UPD-21-F001..F008 | Processing: mills, sawmills, workshops, refineries, ratios, labor, energy, storage | `U21.md` |
| FD-UPD-22-F001..F007 | Telegraph: lines, stations, information/command delay, market/diplomatic communication, repair | `U22.md` |
| FD-UPD-23-F001..F007 | Rail survey: corridors, gradient, curvature, bridges, tunnels, land, estimates | `U23.md` |
| FD-UPD-24-F001..F007 | Rail construction: track, stations, depots, workshops, crews, materials, maintenance | `U24.md` |
| FD-UPD-25-F001..F009 | Trains: locomotives, wagons, consists, capacity, speed, fuel, loading, schedules, depots | `U25.md` |
| FD-UPD-26-F001..F007 | Signals/congestion: blocks, reservations, signals, platforms, junctions, deadlock prevention, throughput | `U26.md` |
| FD-UPD-27-F001..F007 | Ports/maritime: seaports, warehouses, coastal/overseas trade, schedules, congestion, customs | `U27.md` |
| FD-UPD-28-F001..F007 | Mandates: objectives, deadlines, funding, political/shareholder support, evaluation, consequences | `U28.md` |
| FD-UPD-29-F001..F010 | Finance: budget, revenue, expenses, assets, depreciation, loans, interest, default, audit, corruption | `U29.md` |
| FD-UPD-30-F001..F008 | Administration: posts, officials, tax, records, corruption, capacity, reach, maintenance | `U30.md` |
| FD-UPD-31-F001..F007 | Influence: region influence, legitimacy, support, control, dependency, familiarity, rivals | `U31.md` |
| FD-UPD-32-F001..F008 | Intelligence: scouts, informants, reports, confidence, counterintel, espionage, hidden plans, error | `U32.md` |
| FD-UPD-33-F001..F007 | Security: escorts, route security, banditry, raids, convoy defense, patrols, expense | `U33.md` |
| FD-UPD-34-F001..F009 | Formations: columns, militia, infantry, cavalry, engineers, artillery abstraction, readiness, organization, experience | `U34.md` |
| FD-UPD-35-F001..F008 | Military supply: food, ammunition, medicine, replacements, transport, range, attrition, depots | `U35.md` |
| FD-UPD-36-F001..F008 | Fronts: regional fronts, orders, lines, offensives, reserves, objectives, infrastructure, collapse | `U36.md` |
| FD-UPD-37-F001..F008 | War/occupation: battles, withdrawal, surrender, occupation, resistance, civilian effects, burden, treaties | `U37.md` |
| FD-UPD-38-F001..F007 | Resistance/independence: organization, cells, rebellion, movements, negotiation, repression consequences, autonomous states | `U38.md` |
| FD-UPD-39-F001..F007 | Many sovereign states: diplomacy, goals, trade, alliances, rivalries, internal politics, military planning | `U39.md` |
| FD-UPD-40-F001..F006 | Four-player: joint decisions, balance, diplomacy, competition, alliances, PPO attribution | `U40.md` |
| FD-UPD-41-P001 | Eight-player actors with documented performance and memory limits | `U41.md` |
| FD-UPD-42-F001..F010 | Procedural prehistory: states, wars, treaties, trade, dynasties, migrations, grievances, alliances, infrastructure, characters | `U42.md` |
| FD-UPD-43-F001..F008; FD-UPD-43-P002 | Technology: technologies, institutions, transfer, projects, engineering/medical/transport/military advances, no linear ladder | `U43.md` |
| FD-UPD-44-F001..F010 | Dynamic events: drought, flood, epidemic, boom, crisis, succession, civil war, intervention, migration, breakthrough | `U44.md` |
| FD-UPD-45-F001..F007 | Scenario/campaign generator: objectives, starts, constraints, context, difficulty, evaluation seeds, solvability | `U45.md` |
| FD-UPD-46-F001..F008 | Scripted AI: trader, builder, diplomat, expansionist, defender, industrializer, raider, balanced | `U46.md` |
| FD-UPD-47-F001..F007 | PPO/curriculum: hierarchical masks, recurrent PPO, batches, curriculum, seeds, manifests, GPU learner | `U47.md` |
| FD-UPD-48-F001..F008 | League: frozen/main/exploiter/scripted pools, sampling, payoff, non-transitivity, lineage | `U48.md` |
| FD-UPD-49-F001..F009 | Thin web client: human/agent/replay, maps, overlays, characters, summaries, no rules | `U49.md` |
| FD-UPD-50-F001..F015; FD-UPD-50-P001 | Release: stable APIs, mods/validation, Python option, models/cards, benchmarks/reproduction, contributor/security/governance, clean reproduction, signed manifest | `U50.md` |

## Gate Zero deliverable traceability

| DEVSEQ requirement | Evidence |
|---|---|
| Read/hash governing documents | `docs/governance/GOVERNING_DOCUMENTS.md` |
| Repository/build/test/tool baselines and documentation inventory | `docs/baseline/REPOSITORY_BASELINE.md` |
| Requirement traceability | this file |
| Update registry | `docs/updates/UPDATE_REGISTRY.md` |
| Dependency graph | `docs/updates/DEPENDENCY_GRAPH.md` |
| Conflict decisions | `docs/governance/SPECIFICATION_CONFLICTS.md` |
| Architecture decisions | `docs/architecture/DECISION_REGISTER.md` |
| Risks | `docs/governance/RISK_REGISTER.md` |
| Frozen initial slice | `docs/specs/INITIAL_VERTICAL_SLICE.md` |
| C API / save-replay / ASCII / RNG proposals | `docs/architecture/` |
| Test / performance plans | `docs/quality/` |
| Independent approval and complete audit | `docs/evidence/GATE_ZERO_EVIDENCE.md` |



