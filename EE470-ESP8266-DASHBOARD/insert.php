
<?php
/*
 * ----------------------------------------------------
 * Project: EE470 ESP8266 IoT System
 * File: insert.php
 * Version: 2.0
 *
 * Purpose:
 * Validate and store ESP8266 measurements.
 * Support GET and Base64 requests.
 * Prevent duplicate records.
 * Update node activity counters transactionally.
 *
 * Inputs:
 * nodeId, nodeTemp, nodeHumidity,
 * nodeLight, timeReceived
 *
 * Outputs:
 * JSON response indicating success,
 * duplicate, rejection, or server error.
 *
 * Dependencies:
 * PHP, MySQL, MySQLi, db_config.php
 * ----------------------------------------------------
 */

header("Content-Type: application/json; charset=utf-8");
header("Cache-Control: no-store");

mysqli_report(
    MYSQLI_REPORT_ERROR | MYSQLI_REPORT_STRICT
);

// --------------------------------------
// JSON RESPONSE HELPER
// --------------------------------------

function respond($httpCode, $status, $message, $extra = []) {

    http_response_code($httpCode);

    echo json_encode(
        array_merge([
            "status" => $status,
            "message" => $message
        ], $extra),
        JSON_UNESCAPED_SLASHES
    );

    exit;
}

// --------------------------------------
// INPUT DECODING
// --------------------------------------

function getParameters() {

    if ($_SERVER["REQUEST_METHOD"] !== "GET") {
        respond(405, "rejected", "Only GET is supported.");
    }

    // Regular GET:
    // ?nodeId=node_1&nodeTemp=23...
    if (isset($_GET["nodeId"])) {
        return $_GET;
    }

    // Previous assignment's Base64 formats:
    // ?data=BASE64
    // ?BASE64

    $query = $_SERVER["QUERY_STRING"] ?? "";

    if ($query === "") {
        respond(400, "rejected", "Missing parameters.");
    }

    if (strlen($query) > 4096) {
        respond(400, "rejected", "Request too large.");
    }

    if (strpos($query, "data=") === 0) {
        $encoded = substr($query, 5);
    } else {
        $encoded = $query;
    }

    $encoded = rawurldecode($encoded);
    $encoded = str_replace(" ", "+", $encoded);
    $encoded = strtr($encoded, "-_", "+/");

    $decoded = base64_decode($encoded, true);

    if ($decoded === false || $decoded === "") {
        respond(400, "rejected", "Invalid Base64 message.");
    }

    $params = [];
    parse_str($decoded, $params);

    return $params;
}

// --------------------------------------
// REQUIRED FIELD VALIDATION
// --------------------------------------

function requiredString($params, $key) {

    if (
        !isset($params[$key]) ||
        !is_string($params[$key])
    ) {
        respond(400, "rejected", "Missing or invalid: " . $key);
    }

    $value = trim($params[$key]);

    if ($value === "") {
        respond(400, "rejected", "Missing: " . $key);
    }

    return $value;
}

// --------------------------------------
// NUMERIC VALIDATION
// --------------------------------------

function validateNumber(
    $value,
    $name,
    $minimum,
    $maximum
) {

    if (!is_numeric($value)) {
        respond(400, "rejected", $name . " must be numeric.");
    }

    $number = (float)$value;

    if (
        !is_finite($number) ||
        $number < $minimum ||
        $number > $maximum
    ) {
        respond(
            400,
            "rejected",
            $name . " outside allowed range."
        );
    }

    return $number;
}

// --------------------------------------
// TIMESTAMP VALIDATION
// --------------------------------------

function validateTimestamp($value) {

    // Expected:
    // YYYY-MM-DD HH:MM:SS

    $date = DateTimeImmutable::createFromFormat(
        "!Y-m-d H:i:s",
        $value,
        new DateTimeZone("UTC")
    );

    $errors = DateTimeImmutable::getLastErrors();

    if (
        $date === false ||
        $date->format("Y-m-d H:i:s") !== $value ||
        (
            $errors !== false &&
            (
                $errors["warning_count"] > 0 ||
                $errors["error_count"] > 0
            )
        )
    ) {
        respond(
            400,
            "rejected",
            "Invalid timestamp. Use YYYY-MM-DD HH:MM:SS."
        );
    }

    return $value;
}

// --------------------------------------
// MAIN PROGRAM
// --------------------------------------

$conn = null;
$transactionStarted = false;

try {

    // Load credentials outside public_html
    $config = require dirname(__DIR__) . "/db_config.php";

    $conn = new mysqli(
        $config["host"],
        $config["user"],
        $config["password"],
        $config["database"]
    );

    $conn->set_charset("utf8mb4");

    // Read request
    $params = getParameters();

    $node = requiredString($params, "nodeId");
    $tempRaw = requiredString($params, "nodeTemp");
    $humidityRaw = requiredString($params, "nodeHumidity");
    $lightRaw = requiredString($params, "nodeLight");
    $timeRaw = requiredString($params, "timeReceived");

    // Validate registered-node name format
    if (!preg_match('/^node_[0-9]{1,5}$/', $node) ||
        strlen($node) > 10) {

        respond(400, "rejected", "Invalid node name.");
    }

    // Validate measurements
    $temperature = validateNumber(
        $tempRaw, "Temperature", -10, 100
    );

    $humidity = validateNumber(
        $humidityRaw, "Humidity", 0, 100
    );

    $light = validateNumber(
        $lightRaw, "Light", 0, 1023
    );

    // Light ADC must be an integer
    if (floor($light) != $light) {
        respond(
            400,
            "rejected",
            "Light level must be an integer."
        );
    }

    $light = (int)$light;

    // Timestamp must be provided by ESP
    $time = validateTimestamp($timeRaw);

    // ----------------------------------
    // CHECK NODE REGISTRATION
    // ----------------------------------

    $stmt = $conn->prepare(
        "SELECT node_name
         FROM sensor_register
         WHERE node_name = ?"
    );

    $stmt->bind_param("s", $node);
    $stmt->execute();
    $stmt->store_result();

    $registered = ($stmt->num_rows > 0);

    $stmt->close();

    if (!$registered) {
        respond(
            400,
            "rejected",
            "Sensor node is not registered.",
            ["node" => $node]
        );
    }

    // ----------------------------------
    // BEGIN DATABASE TRANSACTION
    // ----------------------------------

    $conn->begin_transaction();
    $transactionStarted = true;

    // ----------------------------------
    // INSERT SENSOR READING
    // ----------------------------------

    $stmt = $conn->prepare(
        "INSERT INTO sensor_data
        (
            node_name,
            time_received,
            temperature,
            humidity,
            light_level
        )
        VALUES (?, ?, ?, ?, ?)"
    );

    $stmt->bind_param(
        "ssddi",
        $node,
        $time,
        $temperature,
        $humidity,
        $light
    );

    try {

        $stmt->execute();

    } catch (mysqli_sql_exception $e) {

        $stmt->close();

        // MySQL duplicate primary key
        if ((int)$e->getCode() === 1062) {

            $conn->rollback();
            $transactionStarted = false;

            respond(
                200,
                "duplicate",
                "Measurement already stored.",
                [
                    "node" => $node,
                    "timestamp" => $time
                ]
            );
        }

        throw $e;
    }

    $stmt->close();

    // ----------------------------------
    // UPDATE ACTIVITY COUNTER
    // ----------------------------------

    $stmt = $conn->prepare(
        "INSERT INTO sensor_activity
            (node_name, message_count)
         VALUES (?, 1)
         ON DUPLICATE KEY UPDATE
            message_count = message_count + 1"
    );

    $stmt->bind_param("s", $node);
    $stmt->execute();
    $stmt->close();

    // ----------------------------------
    // COMMIT BOTH OPERATIONS
    // ----------------------------------

    $conn->commit();
    $transactionStarted = false;

    respond(
        200,
        "success",
        "Measurement stored successfully.",
        [
            "node" => $node,
            "timestamp" => $time,
            "temperature" => $temperature,
            "humidity" => $humidity,
            "light_level" => $light
        ]
    );

} catch (Throwable $e) {

    if ($conn instanceof mysqli) {

        if ($transactionStarted) {
            try {
                $conn->rollback();
            } catch (Throwable $ignored) {
            }
        }

        try {
            $conn->close();
        } catch (Throwable $ignored) {
        }
    }

    // Log server-side details, not database secrets.
    error_log("EE470 insert.php: " . $e->getMessage());

    respond(
        500,
        "error",
        "Database operation failed."
    );
}
