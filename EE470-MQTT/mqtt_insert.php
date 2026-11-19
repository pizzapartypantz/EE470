
<?php
// EE470 MQTT Assignment
// Receives readings from the Python MQTT relay

header("Content-Type: application/json");

if ($_SERVER["REQUEST_METHOD"] !== "POST") {
    http_response_code(405);
    echo json_encode(["error" => "POST required"]);
    exit;
}

// Use a new, private token shared with Python.
// Do not upload the token to GitHub.
$apiToken = "c32b4317b7cd4bdd3d60627a5bb3f964ab6c68d62c294f18be8f9c4786031bc5";

$providedToken = $_SERVER["HTTP_X_API_KEY"] ?? "";

if (!hash_equals($apiToken, $providedToken)) {
    http_response_code(401);
    echo json_encode(["error" => "Unauthorized"]);
    exit;
}

// Load existing database configuration
$config = require __DIR__ . '/../db_config.php';

$host = $config['host'];
$dbname = $config['database'];
$username = $config['user'];
$password = $config['password'];

$input = json_decode(file_get_contents("php://input"), true);

$node = $input["node_name"] ?? "";
$value = $input["potentiometer_value"] ?? null;

if (
    !is_string($node) ||
    !preg_match('/^[A-Za-z0-9_-]{1,50}$/', $node) ||
    filter_var($value, FILTER_VALIDATE_INT) === false ||
    (int)$value < 0 ||
    (int)$value > 1023
) {
    http_response_code(400);
    echo json_encode(["error" => "Invalid input"]);
    exit;
}

try {
    $pdo = new PDO(
        "mysql:host=$host;dbname=$dbname;charset=utf8mb4",
        $username,
        $password,
        [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]
    );

    $stmt = $pdo->prepare(
        "INSERT INTO mqtt_sensor_data
         (node_name, potentiometer_value)
         VALUES (:node, :value)"
    );

    $stmt->execute([
        ":node" => $node,
        ":value" => (int)$value
    ]);

    echo json_encode([
        "success" => true,
        "message" => "MQTT reading stored"
    ]);

} catch (PDOException $e) {
    error_log($e->getMessage());

    http_response_code(500);
    echo json_encode(["error" => "Database operation failed"]);
}
?>
