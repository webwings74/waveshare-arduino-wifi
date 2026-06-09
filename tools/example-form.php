<?php
declare(strict_types=1);

// Change this to your Arduino endpoint on the local network.
$defaultArduinoUrl = 'http://192.168.68.86/api/update';
$requestTimeoutSeconds = 45;

/**
 * Send an HTTP POST request and return a normalized result.
 *
 * @return array{ok:bool,status:int,body:string,error:string,timed_out:bool}
 */
function postRequest(string $url, string $body, string $contentType, int $timeoutSeconds = 8): array
{
	if (function_exists('curl_init')) {
		$ch = curl_init($url);
		if ($ch === false) {
			return ['ok' => false, 'status' => 0, 'body' => '', 'error' => 'Failed to initialize cURL.'];
		}

		curl_setopt_array($ch, [
			CURLOPT_POST => true,
			CURLOPT_POSTFIELDS => $body,
			CURLOPT_HTTPHEADER => [
				'Content-Type: ' . $contentType,
				'Accept: application/json, text/plain, */*',
			],
			CURLOPT_RETURNTRANSFER => true,
			CURLOPT_TIMEOUT => $timeoutSeconds,
			CURLOPT_CONNECTTIMEOUT => 3,
		]);

		$responseBody = curl_exec($ch);
		$curlErrNo = curl_errno($ch);
		$curlError = curl_error($ch);
		$httpStatus = (int) curl_getinfo($ch, CURLINFO_RESPONSE_CODE);
		curl_close($ch);

		if ($responseBody === false) {
			return [
				'ok' => false,
				'status' => $httpStatus,
				'body' => '',
				'error' => $curlError !== '' ? $curlError : 'Unknown cURL error.',
				'timed_out' => ($curlErrNo === CURLE_OPERATION_TIMEDOUT),
			];
		}

		return ['ok' => true, 'status' => $httpStatus, 'body' => (string) $responseBody, 'error' => '', 'timed_out' => false];
	}

	$context = stream_context_create([
		'http' => [
			'method' => 'POST',
			'header' => "Content-Type: {$contentType}\r\nAccept: application/json, text/plain, */*\r\n",
			'content' => $body,
			'timeout' => $timeoutSeconds,
			'ignore_errors' => true,
		],
	]);

	$responseBody = @file_get_contents($url, false, $context);
	if ($responseBody === false) {
		$error = error_get_last();
		$errorMessage = $error['message'] ?? 'HTTP request failed.';
		return [
			'ok' => false,
			'status' => 0,
			'body' => '',
			'error' => $errorMessage,
			'timed_out' => (stripos($errorMessage, 'timed out') !== false),
		];
	}

	return ['ok' => true, 'status' => 0, 'body' => (string) $responseBody, 'error' => '', 'timed_out' => false];
}

function looksLikeJsonUnsupportedResponse(array $response): bool
{
	if ($response['body'] === '') {
		return false;
	}

	$decoded = json_decode($response['body'], true);
	if (!is_array($decoded)) {
		return false;
	}

	$ok = $decoded['ok'] ?? null;
	$message = isset($decoded['message']) ? (string) $decoded['message'] : '';
	if ($ok === false && stripos($message, 'No fields received') !== false) {
		return true;
	}

	return false;
}

$arduinoUrl = $_POST['arduino_url'] ?? $defaultArduinoUrl;
$title = $_POST['title'] ?? '';
$content = $_POST['content'] ?? '';
$status = $_POST['status'] ?? '';

$resultMessage = '';
$resultType = '';
$transportUsed = '';
$responseStatus = null;
$responseBody = '';
$payloadJsonPretty = '';

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
	$payload = [
		'title' => (string) $title,
		'content' => (string) $content,
		'status' => (string) $status,
	];

	$allEmpty = trim($payload['title']) === '' && trim($payload['content']) === '' && trim($payload['status']) === '';
	if ($allEmpty) {
		$resultType = 'error';
		$resultMessage = 'Please fill at least one field (title, content, or status).';
	} elseif (!filter_var($arduinoUrl, FILTER_VALIDATE_URL)) {
		$resultType = 'error';
		$resultMessage = 'The Arduino URL is not a valid URL.';
	} else {
		$jsonBody = json_encode($payload, JSON_UNESCAPED_UNICODE);
		if ($jsonBody === false) {
			$resultType = 'error';
			$resultMessage = 'Failed to encode payload as JSON.';
		} else {
			$payloadJsonPretty = json_encode($payload, JSON_PRETTY_PRINT | JSON_UNESCAPED_UNICODE) ?: $jsonBody;

			// First try JSON, because that is the preferred transport.
			$jsonResponse = postRequest($arduinoUrl, $jsonBody, 'application/json', $requestTimeoutSeconds);
			$transportUsed = 'application/json';

			if (!$jsonResponse['ok']) {
				$resultType = 'error';
				if ($jsonResponse['timed_out']) {
					$resultMessage = 'JSON request timed out. Arduino may still be processing, but no response was received in time.';
				} else {
					$resultMessage = 'Network error while sending JSON: ' . $jsonResponse['error'];
				}
				$responseBody = $jsonResponse['body'];
				$responseStatus = $jsonResponse['status'];
			} else {
				$responseBody = $jsonResponse['body'];
				$responseStatus = $jsonResponse['status'];

				if (looksLikeJsonUnsupportedResponse($jsonResponse)) {
					// Compatibility fallback for firmware that still expects form-urlencoded.
					$formBody = http_build_query($payload, '', '&', PHP_QUERY_RFC3986);
					$transportUsed = 'application/x-www-form-urlencoded (fallback from JSON)';
					$formResponse = postRequest($arduinoUrl, $formBody, 'application/x-www-form-urlencoded', $requestTimeoutSeconds);
					$responseBody = $formResponse['body'];
					$responseStatus = $formResponse['status'];

					if ($formResponse['ok']) {
						$resultType = 'success';
						$resultMessage = 'Sent successfully. JSON was attempted first; fallback was used for compatibility.';
					} elseif ($formResponse['timed_out']) {
						$resultType = 'success';
						$resultMessage = 'Fallback request was sent, but the response timed out. The Arduino is likely still processing the display update.';
					} else {
						$resultType = 'error';
						$resultMessage = 'JSON appears unsupported and fallback failed: ' . $formResponse['error'];
					}
				} else {
					if ($responseStatus !== null && $responseStatus >= 200 && $responseStatus < 300) {
						$resultType = 'success';
						$resultMessage = 'Sent successfully using JSON.';
					} else {
						$resultType = 'error';
						$resultMessage = 'Arduino returned HTTP ' . (string) $responseStatus . ' for JSON request.';
					}
				}
			}
		}
	}
}
?>
<!doctype html>
<html lang="en">
<head>
	<meta charset="utf-8">
	<meta name="viewport" content="width=device-width, initial-scale=1">
	<title>Arduino Update Relay</title>
	<style>
		:root {
			--bg: #f4f6f8;
			--card: #ffffff;
			--ink: #1a1f2b;
			--muted: #5c677d;
			--accent: #0f766e;
			--accent-2: #0a4f4a;
			--ok-bg: #e7f8f0;
			--ok-ink: #0b6b3a;
			--err-bg: #fdebec;
			--err-ink: #a4222a;
			--border: #dbe2ea;
		}

		* { box-sizing: border-box; }

		body {
			margin: 0;
			font-family: "Segoe UI", -apple-system, BlinkMacSystemFont, "Helvetica Neue", Arial, sans-serif;
			background: linear-gradient(160deg, #eef2f7 0%, #f8fafc 100%);
			color: var(--ink);
			padding: 24px;
		}

		.wrap {
			max-width: 860px;
			margin: 0 auto;
		}

		.card {
			background: var(--card);
			border: 1px solid var(--border);
			border-radius: 14px;
			padding: 20px;
			box-shadow: 0 8px 26px rgba(16, 24, 40, 0.06);
		}

		h1 {
			margin: 0 0 8px;
			font-size: 1.5rem;
		}

		p.meta {
			margin: 0 0 18px;
			color: var(--muted);
			line-height: 1.45;
		}

		label {
			display: block;
			margin-top: 12px;
			margin-bottom: 6px;
			font-weight: 600;
		}

		input[type="text"], textarea {
			width: 100%;
			border: 1px solid var(--border);
			border-radius: 10px;
			padding: 10px 12px;
			font-size: 15px;
			font-family: inherit;
			color: var(--ink);
			background: #fff;
		}

		textarea {
			min-height: 130px;
			resize: vertical;
		}

		.actions {
			margin-top: 16px;
		}

		button {
			border: 0;
			border-radius: 10px;
			background: var(--accent);
			color: #fff;
			font-size: 15px;
			font-weight: 600;
			padding: 11px 16px;
			cursor: pointer;
		}

		button:hover {
			background: var(--accent-2);
		}

		.result {
			margin-top: 16px;
			border-radius: 10px;
			padding: 12px;
			line-height: 1.45;
			border: 1px solid transparent;
		}

		.result.success {
			background: var(--ok-bg);
			color: var(--ok-ink);
			border-color: #b5e4cb;
		}

		.result.error {
			background: var(--err-bg);
			color: var(--err-ink);
			border-color: #f5b8bc;
		}

		.codebox {
			margin-top: 14px;
			border: 1px solid var(--border);
			border-radius: 10px;
			background: #0f172a;
			color: #d7e3ff;
			padding: 12px;
			font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, "Liberation Mono", "Courier New", monospace;
			font-size: 13px;
			overflow: auto;
			white-space: pre-wrap;
		}

		.small {
			margin-top: 6px;
			color: var(--muted);
			font-size: 13px;
		}
	</style>
</head>
<body>
<div class="wrap">
	<div class="card">
		<h1>Arduino Web Form Relay</h1>
		<p class="meta">
			Host this page on your server. It accepts internet-facing form input,
			then sends the Waveshare display update to your Arduino over your LAN.
		</p>

		<form method="post">
			<label for="arduino_url">Arduino API URL (local network)</label>
			<input id="arduino_url" name="arduino_url" type="text" value="<?= htmlspecialchars((string) $arduinoUrl, ENT_QUOTES, 'UTF-8') ?>">

			<label for="title">Title</label>
			<input id="title" name="title" type="text" maxlength="63" value="<?= htmlspecialchars((string) $title, ENT_QUOTES, 'UTF-8') ?>">

			<label for="content">Content</label>
			<textarea id="content" name="content" maxlength="256"><?= htmlspecialchars((string) $content, ENT_QUOTES, 'UTF-8') ?></textarea>

			<label for="status">Status</label>
			<input id="status" name="status" type="text" maxlength="63" value="<?= htmlspecialchars((string) $status, ENT_QUOTES, 'UTF-8') ?>">

			<div class="small">Tip: leave status empty to let firmware apply its default status logic.</div>
			<div class="small">Note: full e-paper refresh can take a while; this page waits up to <?= (int) $requestTimeoutSeconds ?> seconds for the Arduino response.</div>

			<div class="actions">
				<button type="submit">Send Update</button>
			</div>
		</form>

		<?php if ($resultMessage !== ''): ?>
			<div class="result <?= $resultType === 'success' ? 'success' : 'error' ?>">
				<strong><?= $resultType === 'success' ? 'Success' : 'Error' ?>:</strong>
				<?= htmlspecialchars($resultMessage, ENT_QUOTES, 'UTF-8') ?>
				<?php if ($transportUsed !== ''): ?>
					<div>Transport: <?= htmlspecialchars($transportUsed, ENT_QUOTES, 'UTF-8') ?></div>
				<?php endif; ?>
				<?php if ($responseStatus !== null): ?>
					<div>HTTP status: <?= (int) $responseStatus ?></div>
				<?php endif; ?>
			</div>
		<?php endif; ?>

		<?php if ($payloadJsonPretty !== ''): ?>
			<div class="small">Payload sent:</div>
			<div class="codebox"><?= htmlspecialchars($payloadJsonPretty, ENT_QUOTES, 'UTF-8') ?></div>
		<?php endif; ?>

		<?php if ($responseBody !== ''): ?>
			<div class="small">Arduino response body:</div>
			<div class="codebox"><?= htmlspecialchars($responseBody, ENT_QUOTES, 'UTF-8') ?></div>
		<?php endif; ?>
	</div>
</div>
</body>
</html>
