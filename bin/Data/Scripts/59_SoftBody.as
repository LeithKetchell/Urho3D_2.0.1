// Soft body physics example.
// This sample demonstrates soft body cloth, rope, and ellipsoid objects.

#include "Scripts/Utilities/Sample.as"

void Start()
{
    // Execute the common startup for samples
    SampleStart();

    // Create the scene content
    CreateScene();

    // Create the UI content
    CreateInstructions();

    // Setup the viewport for displaying the scene
    SetupViewport();

    // Hook up to the frame update events
    SubscribeToEvents();
}

void CreateScene()
{
    scene_ = Scene();

    // Create octree, physics world, debug renderer
    scene_.CreateComponent("Octree");
    scene_.CreateComponent("PhysicsWorld");
    scene_.CreateComponent("DebugRenderer");

    // Create a Zone for ambient lighting
    Node@ zoneNode = scene_.CreateChild("Zone");
    Zone@ zone = zoneNode.CreateComponent("Zone");
    zone.boundingBox = BoundingBox(-1000.0f, 1000.0f);
    zone.ambientColor = Color(0.3f, 0.3f, 0.3f);
    zone.fogColor = Color(0.7f, 0.8f, 0.9f);
    zone.fogStart = 200.0f;
    zone.fogEnd = 400.0f;

    // Create a directional light
    Node@ lightNode = scene_.CreateChild("DirectionalLight");
    lightNode.direction = Vector3(0.6f, -1.0f, 0.8f);
    Light@ light = lightNode.CreateComponent("Light");
    light.lightType = LIGHT_DIRECTIONAL;
    light.brightness = 1.2f;

    // Ground plane
    Node@ floorNode = scene_.CreateChild("Floor");
    floorNode.position = Vector3(0.0f, -0.5f, 0.0f);
    floorNode.scale = Vector3(200.0f, 1.0f, 200.0f);
    StaticModel@ floorModel = floorNode.CreateComponent("StaticModel");
    floorModel.model = cache.GetResource("Model", "Models/Box.mdl");
    floorModel.material = cache.GetResource("Material", "Materials/StoneTiled.xml");
    RigidBody@ floorBody = floorNode.CreateComponent("RigidBody");
    CollisionShape@ floorShape = floorNode.CreateComponent("CollisionShape");
    floorShape.SetBox(Vector3(1.0f, 1.0f, 1.0f));

    // Rigid sphere obstacle (centered under cloth for draping)
    Node@ sphereNode = scene_.CreateChild("Sphere");
    sphereNode.position = Vector3(0.0f, 4.0f, 0.0f);
    sphereNode.scale = Vector3(2.0f, 2.0f, 2.0f);
    StaticModel@ sphereModel = sphereNode.CreateComponent("StaticModel");
    sphereModel.model = cache.GetResource("Model", "Models/Sphere.mdl");
    sphereModel.material = cache.GetResource("Material", "Materials/StoneSmall.xml");
    RigidBody@ sphereBody = sphereNode.CreateComponent("RigidBody");
    CollisionShape@ sphereShape = sphereNode.CreateComponent("CollisionShape");
    sphereShape.SetSphere(1.0f);

    // Cloth (patch)
    Node@ clothNode = scene_.CreateChild("Cloth");
    SoftBody@ cloth = clothNode.CreateComponent("SoftBody");
    cloth.CreatePatch(
        Vector3(-3.0f, 8.0f, -3.0f),
        Vector3(3.0f, 8.0f, -3.0f),
        Vector3(-3.0f, 8.0f, 3.0f),
        Vector3(3.0f, 8.0f, 3.0f),
        16, 16, 3, true);
    cloth.totalMass = 1.0f;
    cloth.linearStiffness = 0.8f;
    cloth.damping = 0.05f;
    cloth.positionIterations = 4;
    cloth.material = cache.GetResource("Material", "Materials/Stone.xml");

    // Rope
    Node@ ropeNode = scene_.CreateChild("Rope");
    SoftBody@ rope = ropeNode.CreateComponent("SoftBody");
    rope.CreateRope(
        Vector3(-5.0f, 4.0f, -5.0f),
        Vector3(5.0f, 4.0f, -5.0f),
        16, 3);
    rope.totalMass = 0.5f;
    rope.linearStiffness = 0.9f;
    rope.damping = 0.02f;
    rope.material = cache.GetResource("Material", "Materials/GreenTransparent.xml");

    // Bouncing ball (ellipsoid with pressure)
    Node@ ballNode = scene_.CreateChild("Ball");
    SoftBody@ ball = ballNode.CreateComponent("SoftBody");
    ball.CreateEllipsoid(
        Vector3(6.0f, 10.0f, 3.0f),
        Vector3(0.8f, 0.8f, 0.8f),
        16);
    ball.totalMass = 2.0f;
    ball.pressure = 100.0f;
    ball.linearStiffness = 0.5f;
    ball.damping = 0.01f;
    ball.material = cache.GetResource("Material", "Materials/Stone.xml");

    // Create the camera
    cameraNode = scene_.CreateChild("Camera");
    cameraNode.CreateComponent("Camera");
    cameraNode.position = Vector3(0.0f, 8.0f, -18.0f);
    cameraNode.rotation = Quaternion(20.0f, 0.0f, 0.0f);
}

void CreateInstructions()
{
    // Construct new Text object, set string to display and font to use
    Text@ instructionText = ui.root.CreateChild("Text");
    instructionText.text = "Use WASD keys and mouse/touch to move\n"
                           "Space to toggle physics debug geometry";
    instructionText.SetFont(cache.GetResource("Font", "Fonts/Anonymous Pro.ttf"), 15);
    instructionText.textAlignment = HA_CENTER;
    instructionText.horizontalAlignment = HA_CENTER;
    instructionText.verticalAlignment = VA_CENTER;
    instructionText.SetPosition(0, ui.root.height / 4);
}

void SetupViewport()
{
    Viewport@ viewport = Viewport(scene_, cameraNode.GetComponent("Camera"));
    renderer.viewports[0] = viewport;
}

void SubscribeToEvents()
{
    SubscribeToEvent("Update", "HandleUpdate");
    SubscribeToEvent("PostRenderUpdate", "HandlePostRenderUpdate");
}

void HandleUpdate(StringHash eventType, VariantMap& eventData)
{
    float timeStep = eventData["TimeStep"].GetFloat();

    // Move the camera
    if (ui.focusElement is null)
    {
        const float MOVE_SPEED = 20.0f;
        const float MOUSE_SENSITIVITY = 0.1f;

        IntVector2 mouseMove = input.mouseMove;
        yaw += MOUSE_SENSITIVITY * mouseMove.x;
        pitch += MOUSE_SENSITIVITY * mouseMove.y;
        pitch = Clamp(pitch, -90.0f, 90.0f);

        cameraNode.rotation = Quaternion(pitch, yaw, 0.0f);

        if (input.keyDown[KEY_W])
            cameraNode.Translate(Vector3(0.0f, 0.0f, 1.0f) * MOVE_SPEED * timeStep);
        if (input.keyDown[KEY_S])
            cameraNode.Translate(Vector3(0.0f, 0.0f, -1.0f) * MOVE_SPEED * timeStep);
        if (input.keyDown[KEY_A])
            cameraNode.Translate(Vector3(-1.0f, 0.0f, 0.0f) * MOVE_SPEED * timeStep);
        if (input.keyDown[KEY_D])
            cameraNode.Translate(Vector3(1.0f, 0.0f, 0.0f) * MOVE_SPEED * timeStep);
    }

    if (input.keyPress[KEY_SPACE])
        drawDebug = !drawDebug;
}

void HandlePostRenderUpdate(StringHash eventType, VariantMap& eventData)
{
    if (drawDebug)
        scene_.GetComponent("PhysicsWorld").DrawDebugGeometry(true);
}

// Script globals
bool drawDebug = false;
