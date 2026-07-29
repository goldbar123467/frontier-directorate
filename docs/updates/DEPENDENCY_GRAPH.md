# Dependency graph for all 50 updates

`docs/updates/dependencies.json` is authoritative and machine-readable. All
listed edges are hard semantic prerequisites: a target cannot enter
`IMPLEMENTING` until each dependency is `VERIFIED`. Every update also integrates
and regression-tests all verified behavior in its starting baseline.

The numbers are topologically valid—no update depends on a later number—but they
do not impose an unnecessary total-order dependency. Independent specification,
test design, or implementation may proceed in parallel only when its hard/schema
prerequisites and the one-update-at-a-time evidence discipline are satisfied.

```text
G0 -> U01 -> U02 -> U03
       |      |      +--> U04 --> U08
       |      +----------> U05
       +-----------------> U06 --> U07

U09 -> U10 -> U11 -> logistics/industry chain U12..U27
                    -> governance/conflict chain U28..U39
U39 -> U40 -> U41
U39 -------------------------------> U42 -> U43 -> U44 -> U45
U41 + U45 -> U46 -> U47 -> U48 -> U49 -> U50
```

The diagram is only a summary; the JSON enumerates every edge.

## Key dependency rationale

- U12 supplies land convoy concepts used by river, road, depot, passenger,
  contract, rail, security, and military logistics.
- U18 follows health, polity, market, depot, passenger, and contract systems so
  settlement change is materially causal.
- U24 follows extractive/processing systems so rail material delivery is real.
- U31 follows polity, finance, administration, and settlement systems so
  influence is not an isolated score.
- U35 follows disease, depots, finance, and formations; U36–U38 deliberately
  order fronts, warfare/occupation, and resistance.
- U40 integrates the mature multi-state surface before four-seat attribution;
  U41 then proves eight-seat bounds.
- U45 depends on world history/events and maximum-seat support so its solvability
  validator targets the actual campaign surface.
- U47 follows scripted baselines and campaign generation; PPO is never the first
  client to exercise the environment.
- U49 remains thin because rules, actions, replays, and agents already exist on
  the authority side. U50 is transitively last.

## Transition and regression rule

At any moment, the earliest dependency-valid work is the lowest-numbered
`SPECIFIED` update whose hard prerequisites are all `VERIFIED`. Gate Zero Warden
approval satisfies only U01's `G0` edge; it does not verify U01.

If a prerequisite becomes `REGRESSION`, all descendants are impact-audited.
Affected descendants become `REGRESSION`; unaffected descendants retain state
only with recorded Warden evidence. The JSON is validated for exactly U01–U50,
known nodes, uniqueness, lower-number dependencies, and acyclicity.

