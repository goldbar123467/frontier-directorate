# Complete prose-paragraph catalog

Source: GOV-001, SHA-256 5235d46f948d8d233cecf61fdfb18622c02977935168578fbab46c6d10f21fb6.

This generated companion catalogs every prose paragraph outside Markdown
headings, lists, tables, and fenced examples. It deliberately includes both
normative and explanatory prose; governing wording controls modality. List
children are traced in `ATOMIC_REQUIREMENT_CATALOG.md`. Together the catalogs
give every binding prose statement a stable ID, exact source line allocation,
and planned evidence destination.

IDs are frozen only when Gate Zero passes. Later source changes use explicit
supersession records and never silently reuse IDs.

| Requirement ID | Source line(s) | Source context | Exact normalized paragraph | Planned evidence allocation |
|---|---:|---|---|---|
| FD-S00-P001 | 5 | Preamble | You are the program director for **Frontier Directorate**, a terminal-first, procedurally generated grand-strategy logistics game and reinforcement-learning research environment written primarily in C and C++. | governing review |
| FD-S00-P002 | 7 | Preamble | The project combines: | governing review |
| FD-S00-P003 | 14 | Preamble | The initial historical inspiration is nineteenth-century imperial, commercial, and state competition across a procedurally generated fictional continent. | governing review |
| FD-S00-P004 | 16 | Preamble | The world must not portray local societies as empty land, passive resources, or interchangeable obstacles. The generated continent contains sovereign polities, settlements, economies, languages, trade networks, alliances, conflicts, institutions, and historical trajectories that exist independently of foreign expeditions. | governing review |
| FD-S00-P005 | 18 | Preamble | Factions may include: | governing review |
| FD-S00-P006 | 35 | Preamble | No faction type is inherently destined to dominate, modernize, disappear, or submit. | governing review |
| FD-S00-P007 | 37 | Preamble | The simulation may model imperialism, unequal treaties, extraction, resistance, warfare, collaboration, industrialization, and political coercion. It must model their material consequences rather than treating conquest as morally neutral map painting. | governing review |
| FD-S01-P001 | 43 | Intellectual-Property Boundary | The project may take inspiration from the design principles of Dwarf Fortress, OpenTTD, Hearts of Iron IV, RimWorld, and other simulation games. | license/provenance review |
| FD-S01-P002 | 45 | Intellectual-Property Boundary | It must not copy: | license/provenance review |
| FD-S01-P003 | 63 | Intellectual-Property Boundary | The ASCII interface must be independently designed. | license/provenance review |
| FD-S01-P004 | 65 | Intellectual-Property Boundary | “Inspired by Dwarf Fortress ASCII” means: | license/provenance review |
| FD-S01-P005 | 76 | Intellectual-Property Boundary | It does not mean cloning Dwarf Fortress. | license/provenance review |
| FD-S02-P001 | 82 | Primary Product Objective | Build a complete game and research library containing: | update evidence and final manifest |
| FD-S02-P002 | 103 | Primary Product Objective | The project is not complete until all 50 updates are: | update evidence and final manifest |
| FD-S02-P003 | 116 | Primary Product Objective | Documentation, scaffolding, interfaces, placeholders, feature flags, and demonstration-only code do not count as completed updates. | update evidence and final manifest |
| FD-S03-P001 | 122 | Initial Vertical Slice | The first playable version is deliberately small. | VS1 integration evidence (U01-U28) |
| FD-S03-P002 | 183 | Initial Vertical Slice | Each foreign faction competes to establish a reliable and politically sustainable corridor between the coastal and inland settlements. | VS1 integration evidence (U01-U28) |
| FD-S03-P003 | 185 | Initial Vertical Slice | Success depends on: | VS1 integration evidence (U01-U28) |
| FD-S03-P004 | 196 | Initial Vertical Slice | Military conquest is not required for victory. | VS1 integration evidence (U01-U28) |
| FD-S04-P001 | 202 | Two-Level World Architecture | Use two linked representations. | tile/region invariant tests |
| FD-S04-P002 | 206 | Two-Level World Architecture | The tile world represents local physical reality: | tile/region invariant tests |
| FD-S04-P003 | 223 | Two-Level World Architecture | The tile world is authoritative for: | tile/region invariant tests |
| FD-S04-P004 | 236 | Two-Level World Architecture | The region graph represents strategic relationships: | tile/region invariant tests |
| FD-S04-P005 | 250 | Two-Level World Architecture | The region graph is authoritative for: | tile/region invariant tests |
| FD-S04-P006 | 259 | Two-Level World Architecture | Neither representation may silently contradict the other. | tile/region invariant tests |
| FD-S04-P007 | 261 | Two-Level World Architecture | All graph edges must correspond to valid transport or political relationships in the underlying world. | tile/region invariant tests |
| FD-S05-P001 | 267 | Authoritative C Simulation Core | The C engine owns all authoritative game state and transitions. | C core/API tests and update reports |
| FD-S05-P002 | 269 | Authoritative C Simulation Core | It must own: | C core/API tests and update reports |
| FD-S05-P003 | 306 | Authoritative C Simulation Core | The C core must: | C core/API tests and update reports |
| FD-S06-P001 | 327 | C++ Systems and ML Layer | The C++ layer owns: | C++ boundary/orchestration tests |
| FD-S06-P002 | 346 | C++ Systems and ML Layer | The C++ layer must not implement alternative game rules. | C++ boundary/orchestration tests |
| FD-S06-P003 | 348 | C++ Systems and ML Layer | C++ code must: | C++ boundary/orchestration tests |
| FD-S07-P001 | 363 | Compute Architecture | Do not assume that C or C++ automatically means GPU acceleration. | performance plan; U47 backend evidence |
| FD-S07-P002 | 365 | Compute Architecture | The initial simulation is CPU-first. | performance plan; U47 backend evidence |
| FD-S07-P003 | 367 | Compute Architecture | The eventual learning architecture must support: | performance plan; U47 backend evidence |
| FD-S07-P004 | 375 | Compute Architecture | Do not prematurely introduce CUDA architecture during the initial world and game-engine phases. | performance plan; U47 backend evidence |
| FD-S07-P005 | 377 | Compute Architecture | During the PPO architecture phase, explicitly decide: | performance plan; U47 backend evidence |
| FD-S07-P006 | 392 | Compute Architecture | Custom GPU kernels require a measured bottleneck and an independently correct reference implementation. | performance plan; U47 backend evidence |
| FD-S08-P001 | 398 | Procedural World Requirements | Every generated world must be reproducible from: | world-generation update/property evidence |
| FD-S08-P002 | 405 | Procedural World Requirements | Generation must be bounded and return structured errors. | world-generation update/property evidence |
| FD-S08-P003 | 407 | Procedural World Requirements | The generator must create: | world-generation update/property evidence |
| FD-S08-P004 | 427 | Procedural World Requirements | Generated geography must satisfy: | world-generation update/property evidence |
| FD-S08-P005 | 439 | Procedural World Requirements | The generator must preserve interesting asymmetry without creating undefined or unwinnable normal scenarios. | world-generation update/property evidence |
| FD-S09-P001 | 445 | Historical World Simulation | Before player arrival, generate a bounded procedural history. | U42 history evidence |
| FD-S09-P002 | 447 | Historical World Simulation | Potential historical processes include: | U42 history evidence |
| FD-S09-P003 | 464 | Historical World Simulation | History generation must produce current conditions and causal records. | U42 history evidence |
| FD-S09-P004 | 466 | Historical World Simulation | It must not generate decorative lore disconnected from simulation state. | U42 history evidence |
| FD-S09-P005 | 468 | Historical World Simulation | Generated history should affect: | U42 history evidence |
| FD-S10-P001 | 485 | ASCII Interface Requirements | The terminal interface is the primary initial client. | ASCII specification and owning update |
| FD-S11-P001 | 559 | Simulation Time | Use deterministic discrete simulation time. | determinism/time tests |
| FD-S11-P002 | 561 | Simulation Time | Separate: | determinism/time tests |
| FD-S11-P003 | 570 | Simulation Time | The update order must be explicitly specified. | determinism/time tests |
| FD-S11-P004 | 572 | Simulation Time | A provisional order is: | determinism/time tests |
| FD-S11-P005 | 598 | Simulation Time | No subsystem may depend on unordered container iteration or thread timing. | determinism/time tests |
| FD-S12-P001 | 604 | Integer and Fixed-Point Policy | Use integer or fixed-point representations for authoritative state where practical. | numeric tests |
| FD-S12-P002 | 606 | Integer and Fixed-Point Policy | This includes: | numeric tests |
| FD-S12-P003 | 622 | Integer and Fixed-Point Policy | Floating-point arithmetic may be used only where: | numeric tests |
| FD-S12-P004 | 629 | Integer and Fixed-Point Policy | All authoritative arithmetic must be checked for overflow. | numeric tests |
| FD-S13-P001 | 635 | Political and Ethical Simulation Constraints | The game may model violent and exploitative historical systems, but must not encode them as consequence-free optimization bonuses. | mechanics invariants and independent review |
| FD-S13-P002 | 637 | Political and Ethical Simulation Constraints | The simulation must represent consequences such as: | mechanics invariants and independent review |
| FD-S13-P003 | 651 | Political and Ethical Simulation Constraints | Local populations must be represented as political and economic actors. | mechanics invariants and independent review |
| FD-S13-P004 | 653 | Political and Ethical Simulation Constraints | They may: | mechanics invariants and independent review |
| FD-S13-P005 | 670 | Political and Ethical Simulation Constraints | The game may not assume that foreign administration is inherently more advanced, legitimate, efficient, or historically inevitable. | mechanics invariants and independent review |
| FD-S14-P001 | 676 | Agent Action Architecture | Do not use one enormous flat action index. | action/mask transactional tests |
| FD-S14-P002 | 678 | Agent Action Architecture | Use hierarchical actions. | action/mask transactional tests |
| FD-S14-P003 | 680 | Agent Action Architecture | Top-level categories: | action/mask transactional tests |
| FD-S14-P004 | 697 | Agent Action Architecture | Examples: | action/mask transactional tests |
| FD-S14-P005 | 740 | Agent Action Architecture | Each parameter stage requires an exact legal mask. | action/mask transactional tests |
| FD-S14-P006 | 742 | Agent Action Architecture | The engine must independently validate the final action. | action/mask transactional tests |
| FD-S14-P007 | 744 | Agent Action Architecture | Invalid actions must: | action/mask transactional tests |
| FD-S15-P001 | 757 | Observation Architecture | Observations must separate: | observation schema/leakage tests |
| FD-S15-P002 | 774 | Observation Architecture | Every feature requires: | observation schema/leakage tests |
| FD-S15-P003 | 787 | Observation Architecture | Information leakage tests are mandatory. | observation schema/leakage tests |
| FD-S15-P004 | 789 | Observation Architecture | Future events, hidden opponent actions, internal RNG state, debug values, and omniscient map data may not enter ordinary actor observations. | observation schema/leakage tests |
| FD-S16-P001 | 795 | Reinforcement-Learning Requirements | Do not implement PPO before the environment passes its correctness gates. | U47/U48 learning evidence |
| FD-S16-P002 | 797 | Reinforcement-Learning Requirements | The eventual trainer must support: | U47/U48 learning evidence |
| FD-S16-P003 | 816 | Reinforcement-Learning Requirements | PPO correctness must be validated on: | U47/U48 learning evidence |
| FD-S16-P004 | 830 | Reinforcement-Learning Requirements | Learning is not proof of correctness. | U47/U48 learning evidence |
| FD-S17-P001 | 838 | Multi-Agent Development Model | The Queen is the program director. | review provenance |
| FD-S17-P002 | 840 | Multi-Agent Development Model | The Queen: | review provenance |
| FD-S17-P003 | 853 | Multi-Agent Development Model | The Warden is the verification authority. | review provenance |
| FD-S17-P004 | 855 | Multi-Agent Development Model | The Warden: | review provenance |
| FD-S17-P005 | 869 | Multi-Agent Development Model | Use specialized workers: | review provenance |
| FD-S17-P006 | 896 | Multi-Agent Development Model | Critical code may not be accepted solely by its author. | review provenance |
| FD-S18-P001 | 902 | Mandatory Evidence Per Update | Every update must produce: | per-update evidence reports |
| FD-S18-P002 | 990 | Mandatory Evidence Per Update | Only the Warden may assign `VERIFIED`. | per-update evidence reports |
| FD-S19-P001 | 996 | Strict Prohibitions | Agents must not: | automated policy checks and Warden audit |
| FD-UPD-01-P001 | 1045 | Update 01 — Procedural Coastal Corridor | Implement the initial world: | docs/evidence/updates/U01.md |
| FD-UPD-02-P001 | 1059 | Update 02 — Expedition Personnel | Add: | docs/evidence/updates/U02.md |
| FD-UPD-03-P001 | 1074 | Update 03 — Expedition Inventory | Add: | docs/evidence/updates/U03.md |
| FD-UPD-04-P001 | 1088 | Update 04 — Camps and Rest | Add: | docs/evidence/updates/U04.md |
| FD-UPD-05-P001 | 1100 | Update 05 — Exploration and Fog of War | Add: | docs/evidence/updates/U05.md |
| FD-UPD-06-P001 | 1112 | Update 06 — Terrain Movement | Add movement effects for: | docs/evidence/updates/U06.md |
| FD-UPD-07-P001 | 1125 | Update 07 — Weather and Seasons | Add: | docs/evidence/updates/U07.md |
| FD-UPD-08-P001 | 1138 | Update 08 — Disease | Add: | docs/evidence/updates/U08.md |
| FD-UPD-09-P001 | 1152 | Update 09 — Local Polity Simulation | Add: | docs/evidence/updates/U09.md |
| FD-UPD-10-P001 | 1166 | Update 10 — Diplomacy and Access | Add: | docs/evidence/updates/U10.md |
| FD-UPD-11-P001 | 1180 | Update 11 — Trade and Markets | Add: | docs/evidence/updates/U11.md |
| FD-UPD-12-P001 | 1193 | Update 12 — Porter and Caravan Logistics | Add: | docs/evidence/updates/U12.md |
| FD-UPD-13-P001 | 1206 | Update 13 — River Transport | Add: | docs/evidence/updates/U13.md |
| FD-UPD-14-P001 | 1219 | Update 14 — Roads | Add: | docs/evidence/updates/U14.md |
| FD-UPD-15-P001 | 1232 | Update 15 — Supply Depots | Add: | docs/evidence/updates/U15.md |
| FD-UPD-16-P001 | 1244 | Update 16 — Passenger Transport | Add: | docs/evidence/updates/U16.md |
| FD-UPD-17-P001 | 1258 | Update 17 — Cargo Contracts | Add: | docs/evidence/updates/U17.md |
| FD-UPD-18-P001 | 1270 | Update 18 — Settlement Growth | Add: | docs/evidence/updates/U18.md |
| FD-UPD-19-P001 | 1284 | Update 19 — Agriculture | Add: | docs/evidence/updates/U19.md |
| FD-UPD-20-P001 | 1297 | Update 20 — Extractive Industries | Add: | docs/evidence/updates/U20.md |
| FD-UPD-21-P001 | 1310 | Update 21 — Processing Industries | Add: | docs/evidence/updates/U21.md |
| FD-UPD-22-P001 | 1323 | Update 22 — Telegraph and Communications | Add: | docs/evidence/updates/U22.md |
| FD-UPD-23-P001 | 1335 | Update 23 — Rail Surveying | Add: | docs/evidence/updates/U23.md |
| FD-UPD-24-P001 | 1347 | Update 24 — Rail Construction | Add: | docs/evidence/updates/U24.md |
| FD-UPD-25-P001 | 1359 | Update 25 — Trains | Add: | docs/evidence/updates/U25.md |
| FD-UPD-26-P001 | 1373 | Update 26 — Rail Signals and Congestion | Add: | docs/evidence/updates/U26.md |
| FD-UPD-27-P001 | 1385 | Update 27 — Ports and Maritime Trade | Add: | docs/evidence/updates/U27.md |
| FD-UPD-28-P001 | 1397 | Update 28 — Government Mandates | Add: | docs/evidence/updates/U28.md |
| FD-UPD-29-P001 | 1409 | Update 29 — Company and State Finance | Add: | docs/evidence/updates/U29.md |
| FD-UPD-30-P001 | 1424 | Update 30 — Administration | Add: | docs/evidence/updates/U30.md |
| FD-UPD-31-P001 | 1437 | Update 31 — Political Influence | Add: | docs/evidence/updates/U31.md |
| FD-UPD-32-P001 | 1449 | Update 32 — Intelligence | Add: | docs/evidence/updates/U32.md |
| FD-UPD-33-P001 | 1462 | Update 33 — Security and Escorts | Add: | docs/evidence/updates/U33.md |
| FD-UPD-34-P001 | 1474 | Update 34 — Military Formations | Add: | docs/evidence/updates/U34.md |
| FD-UPD-35-P001 | 1488 | Update 35 — Military Supply | Add: | docs/evidence/updates/U35.md |
| FD-UPD-36-P001 | 1501 | Update 36 — Strategic Fronts | Add: | docs/evidence/updates/U36.md |
| FD-UPD-37-P001 | 1514 | Update 37 — Warfare and Occupation | Add: | docs/evidence/updates/U37.md |
| FD-UPD-38-P001 | 1527 | Update 38 — Resistance and Independence | Add: | docs/evidence/updates/U38.md |
| FD-UPD-39-P001 | 1539 | Update 39 — Multiple Sovereign States | Expand the world to many local and foreign factions with: | docs/evidence/updates/U39.md |
| FD-UPD-40-P001 | 1551 | Update 40 — Four-Player Grand Strategy | Support four simultaneous strategic actors with: | docs/evidence/updates/U40.md |
| FD-UPD-41-P001 | 1562 | Update 41 — Eight-Player Grand Strategy | Support eight major strategic actors while maintaining documented performance and memory limits. | docs/evidence/updates/U41.md |
| FD-UPD-42-P001 | 1566 | Update 42 — Procedural History | Generate pregame: | docs/evidence/updates/U42.md |
| FD-UPD-43-P001 | 1581 | Update 43 — Technology and Research | Add: | docs/evidence/updates/U43.md |
| FD-UPD-43-P002 | 1592 | Update 43 — Technology and Research | Technology may not be modeled as a single linear civilizational ladder. | docs/evidence/updates/U43.md |
| FD-UPD-44-P001 | 1596 | Update 44 — Dynamic World Events | Add: | docs/evidence/updates/U44.md |
| FD-UPD-45-P001 | 1611 | Update 45 — Scenario and Campaign Generator | Generate structured campaigns with: | docs/evidence/updates/U45.md |
| FD-UPD-46-P001 | 1623 | Update 46 — Scripted AI Factions | Implement strong deterministic and stochastic baselines: | docs/evidence/updates/U46.md |
| FD-UPD-47-P001 | 1636 | Update 47 — PPO and Curriculum Training | Implement and validate: | docs/evidence/updates/U47.md |
| FD-UPD-48-P001 | 1648 | Update 48 — Historical League Self-Play | Add: | docs/evidence/updates/U48.md |
| FD-UPD-49-P001 | 1661 | Update 49 — Web Client and Multiplayer Viewer | Add a thin browser client supporting: | docs/evidence/updates/U49.md |
| FD-UPD-50-P001 | 1675 | Update 50 — Public Research and Modding Release | Complete: | docs/evidence/updates/U50.md |
| FD-S21-P001 | 1697 | Update Registry | Create: | update registry validation |
| FD-S21-P002 | 1703 | Update Registry | Allowed states: | update registry validation |
| FD-S21-P003 | 1716 | Update Registry | Only the Warden may assign `VERIFIED`. | update registry validation |
| FD-S21-P004 | 1718 | Update Registry | If a later change breaks a verified update, mark it `REGRESSION`. | update registry validation |
| FD-S21-P005 | 1720 | Update Registry | No final release may contain any state other than `VERIFIED` across all 50 updates. | update registry validation |
| FD-S22-P001 | 1726 | Development Sequence | Before production implementation: | Gate Zero evidence |
| FD-S22-P002 | 1746 | Development Sequence | “Begin with specification only” means production changes are forbidden before this gate passes. It does not terminate the complete assignment. | Gate Zero evidence |
| FD-S22-P003 | 1748 | Development Sequence | After the gate passes, proceed automatically through the earliest dependency-valid work. | Gate Zero evidence |
| FD-S22-P004 | 1750 | Development Sequence | Do not stop merely to ask whether to continue. | Gate Zero evidence |
| FD-S23-P001 | 1756 | Performance Requirements | Measure, at minimum: | raw benchmark artifacts |
| FD-S23-P002 | 1776 | Performance Requirements | No performance claim is valid without: | raw benchmark artifacts |
| FD-S23-P003 | 1788 | Performance Requirements | Optimization may not alter rules. | raw benchmark artifacts |
| FD-S24-P001 | 1794 | Final Verification Campaign | After Update 50, run: | final verification artifacts |
| FD-S24-P002 | 1825 | Final Verification Campaign | Create: | final verification artifacts |
| FD-S24-P003 | 1832 | Final Verification Campaign | The final report must contain exactly 50 update rows. | final verification artifacts |
| FD-S24-P004 | 1834 | Final Verification Campaign | Every row must be `VERIFIED`. | final verification artifacts |
| FD-S25-P001 | 1840 | First Instruction to the Agent Team | Begin with specification ingestion and repository baseline only. | Gate Zero evidence |
| FD-S25-P002 | 1842 | First Instruction to the Agent Team | Do not write production code yet. | Gate Zero evidence |
| FD-S25-P003 | 1844 | First Instruction to the Agent Team | Produce: | Gate Zero evidence |
| FD-S25-P004 | 1870 | First Instruction to the Agent Team | Do not treat missing implementation as an external blocker. | Gate Zero evidence |
| FD-S25-P005 | 1872 | First Instruction to the Agent Team | After internal blocking specification findings are resolved and independently verified, begin Update 01 automatically. | Gate Zero evidence |
| FD-S26-P001 | 1878 | Final Directive | Do not optimize for visible activity, code volume, file count, or apparent speed. | final evidence manifest |
| FD-S26-P002 | 1880 | Final Directive | Optimize for: | final evidence manifest |
| FD-S26-P003 | 1895 | Final Directive | The project is not complete because fifty update documents exist. | final evidence manifest |
| FD-S26-P004 | 1897 | Final Directive | It is not complete because fifty interfaces compile. | final evidence manifest |
| FD-S26-P005 | 1899 | Final Directive | It is not complete because fifty demonstrations can be staged. | final evidence manifest |
| FD-S26-P006 | 1901 | Final Directive | It is complete only when all fifty updates are implemented, integrated, tested, benchmarked where relevant, independently verified, and represented as `VERIFIED` in the final immutable evidence manifest. | final evidence manifest |

