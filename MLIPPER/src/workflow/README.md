# External Workflow Adapters

This directory contains narrow adapters between MLIPPER's internal session API
and external workflow components. It should not own long-lived GPU/tree state.

## DIPPER Starting Tree

`dipper_starting_tree.hpp/.cpp` is the maintained adapter to the pinned DIPPER
source in `third_party/dipper`.

- `DipperTreeMode::NJPlacement`: request DIPPER's neighbor-joining plus
  placement starting-tree route.
- `DipperTreeMode::DivideAndConquer`: request DIPPER's divide-and-conquer
  starting-tree route.
- `buildDipperStartingTree()`: convert a validated `parse::Alignment` to the
  DIPPER input form, run the selected construction method, and return Newick.

The function returns a string because `MlipperSession::loadBackboneTree()` owns
the next step: parsing the result into MLIPPER's `TreeBuildResult`.

## D&C Call Position

```text
main.cpp
  -> estimate and reserve GPU
  -> buildDipperStartingTree()
  -> MlipperSession::loadBackboneTree()
  -> initializeDivideAndConquerGPUWithReservation()
```

GPU selection deliberately happens before calling DIPPER so the same
`DeviceReservation` covers starting-tree construction and all later MLIPPER
D&C/final-optimization stages.

## Boundary Rule

Keep this adapter thin. DIPPER-specific conversions belong here; MLIPPER tree
packing belongs in `tree/`, CLI policy belongs in `io/`, and D&C sector
optimization belongs in `MlipperSession` plus `tree/divide_and_conquer.*`.
