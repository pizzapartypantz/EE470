
<?php
// EE470 MQTT Assignment
// Return the latest 100 potentiometer readings

header("Content-Type: application/json");
header("Cache-Control: no-store");

try {
    // Load existing Hostinger database configuration
    $config = require __DIR__ . '/../db_config.php';

    $pdo = new PDO(
        "mysql:host={$config['host']};" .
        "dbname={$config['database']};charset=utf8mb4",
        $config['user'],
        $config['password'],
        [
            PDO::ATTR_ERRMODE =>
                PDO::ERRMODE_EXCEPTION
        ]
    );

$pdo->exec("SET time_zone = '+00:00'");

    // Retrieve newest 100 readings,
    // then display them chronologically.
    $stmt = $pdo->query(
        "SELECT id, node_name,
                potentiometer_value,
                time_received
         FROM (
             SELECT id, node_name,
                    potentiometer_value,
                    time_received
             FROM mqtt_sensor_data
             ORDER BY id DESC
             LIMIT 100
         ) AS recent
         ORDER BY id ASC"
    );

    $readings = $stmt->fetchAll(PDO::FETCH_ASSOC);

    echo json_encode($readings);

} catch (Throwable $e) {
    error_log($e->getMessage());

    http_response_code(500);

    echo json_encode([
        "error" => "Unable to retrieve MQTT data"
    ]);
}
?>
