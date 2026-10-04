# Orphaned Bullet Samples — Awaiting Review

These 10 samples were ported from Bullet3 but don't map to a top-level directory in
https://github.com/bulletphysics/bullet3/tree/master/examples

They need investigation to determine their exact origin (which sub-directory, which
source file in the Bullet3 repo) before being assigned a number in the new scheme.

## Samples

| Old # | Name | Suspected Origin | Status |
|-------|------|-----------------|--------|
| 04 | BS_Stacking | RigidBody/ ? | PASS |
| 05 | BS_Domino | ExtendedTutorials/ ? | PASS |
| 06 | BS_CollisionFilter | RigidBody/ ? | PASS |
| 07 | BS_Kinematic | RigidBody/ ? | PASS |
| 10 | BS_MotorDemo | DynamicControlDemo/ ? | ISSUES (spider seizure) |
| 12 | BS_SoftContact | RigidBody/ ? | PASS |
| 13 | BS_Pendulum | ExtendedTutorials/ ? | PASS |
| 14 | BS_Chain | ExtendedTutorials/ ? | PASS |
| 17 | BS_PlanetGravity | ExtendedTutorials/ ? | PASS |
| 19 | BS_Hinge2Vehicle | Vehicles/ ? | PASS (note) |

## TODO
- Trace each sample back to its exact source file in bullet3/examples/
- Determine if it should be folded into its parent directory's sample or kept standalone
- Reassign numbers once parent mapping is confirmed
