# BulletSamples

Ports of Bullet Physics SDK examples into Urho3D. Each sample faithfully
reproduces the physics setup from the original Bullet source — same shapes,
masses, positions, forces, and constraint parameters. Where Urho3D didn't
wrap a Bullet feature, the engine was extended rather than the demo simplified.

Original physics code by Erwin Coumans, released under the zlib license.
Ported to Urho3D by Leith Ketchell and Claude (Anthropic).

## Samples

| Sample | Original | What it shows |
|--------|----------|---------------|
| BS_HelloPhysics | — | Minimal physics scene — the "hello world" of rigid bodies |
| BS_BasicDemo | BasicExample.cpp | 125 falling cubes in a 5x5x5 grid |
| BS_Constraints | ConstraintDemo.cpp | All 14 constraint setups (P2P, hinge, slider, cone-twist, 6DOF, spring, gear) |
| BS_RollingFriction | RollingFrictionDemo.cpp | 125 shapes on a slope with rolling + spinning friction |
| BS_Gyroscopic | GyroscopicSetup.cpp | 4 gyroscopic force modes in zero gravity |
| BS_Raycast | RaytestDemo.cpp | All-hits and closest-hit raycasting against 6 shapes |
| BS_Kinematic | KinematicRigidBodyExample.cpp | Rotating kinematic platform with 125 falling boxes |
| BS_SoftContact | RigidBodySoftContact.cpp | Compliant ground with contact stiffness and damping |
| BS_ForkLift | ForkLiftDemo.cpp | Raycast vehicle with lift hinge and fork slider — full Bullet btRaycastVehicle |
| BS_Chain | — | 15 hinge-linked rigid bodies as a chain bridge, heavy ball dropped on it |
| BS_Domino | — | 50 dominoes in a spiral, triggered by a ball impulse |
| BS_MotorDemo | MotorDemo.cpp | 6-legged walking spider with sinusoidal hinge motor control |
| BS_Planar2D | Planar2D.cpp | 2D physics — bodies constrained to XY plane via linear/angular factors |
| BS_PlanetGravity | — | Radial gravity — per-body gravity pointing toward a planet center |

## Engine features added during porting

- **Constraint types**: 6DOF, 6DOF Spring, Gear, 6DOF Spring2 — with per-axis
  limits, motors, springs, and gear ratio
- **RigidBody**: spinning friction, contact stiffness/damping
- **AngelScript**: all new types and methods bound

## Building

```
cmake .. -DURHO3D_BULLET_SAMPLES=1
make -j4 BS_BasicDemo BS_Constraints  # build specific samples
make -j4 $(make help | grep BS_ | awk '{print $2}')  # build all
```

Binaries go to `build/bin/BulletSamples/`. Resource symlinks (Data, CoreData)
are created automatically by CMake.

## Raw Bullet access

Some features aren't wrapped by Urho3D yet. These samples reach through
to the raw Bullet API where needed:

- `PhysicsWorld::GetWorld()` → `btDiscreteDynamicsWorld*` — vehicle physics
- `RigidBody::GetBody()` → `btRigidBody*` — gyroscopic flags, activation state
- `Constraint::GetConstraint()` → `btTypedConstraint*` — hinge motors, breaking joints
- `btRaycastVehicle` — full vehicle with wheel tuning, suspension, steering
- `btHingeConstraint::enableAngularMotor()` — sinusoidal motor control
- `btConeTwistConstraint` softness — not yet wrapped
