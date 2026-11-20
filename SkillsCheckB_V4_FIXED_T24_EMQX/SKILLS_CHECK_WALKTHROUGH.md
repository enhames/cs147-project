# Skills Check B V4 Walkthrough and Rubric Answers

## The planted defect

The starter checks the exact Team 24 command topic and validates the servo angle, but it accepts `servo:<angle>` without proving that the command came from an authorized operator. TLS authenticates the broker, and the MQTT login authenticates a broker client, but any client account that EMQX allows to publish to `iot/T24/cmd` can operate the servo.

## Fixed command format

`auth:<your provisioned token>;servo:<angle>`

Example shape only: `auth:YOUR_PRIVATE_TOKEN;servo:90`

Never commit the real token. Enter it during `PROVISION`; the firmware stores it in NVS.

## Positive and negative tests

1. Build, upload, and open Serial Monitor at 115200.
2. Type `PROVISION` during the startup window and enter Wi-Fi, EMQX, and command-token values.
3. Confirm `STATUS` reports BH1750 `ok`, Wi-Fi connected, and MQTT connected.
4. Publish the old unauthenticated payload `servo:45` to `iot/T24/cmd`.
   Expected: `CMD rejected: command authorization failed`; servo does not move.
5. Publish `auth:<correct-token>;servo:45`.
   Expected: `CMD accepted: authenticated servo command`; servo moves to 45 degrees.
6. Publish a correct token to the wrong topic, such as `iot/T24/lux`.
   Expected: no servo movement because the device is subscribed only to `iot/T24/cmd`.
7. Cover and uncover the BH1750. Confirm lux continues to publish every five seconds.

## Rubric answer: identify and explain

The defect is an unauthenticated application command path. The code uses verified TLS and checks the Team 24 topic, but the original callback accepts any `servo:<angle>` payload from a client that can publish to that topic. An unauthorized broker client could move the physical servo. Topic names and input validation are not command authentication.

## Rubric answer: fix without breaking the device

We added an application command token that is entered through Serial and stored in ESP32 NVS. The callback now requires `auth:<token>;servo:<angle>`, checks the exact command topic, checks the token, validates the command name, and enforces the 0–180 degree angle range. The BH1750, TLS, MQTT reconnect logic, lux publication, Last Will, and authorized servo behavior remain working. EMQX should also use an authorization ACL that permits only the operator account to publish to `iot/T24/cmd` and only the device account to subscribe. EMQX Cloud supports authorization rules by username or client ID, topic, and publish/subscribe action.

## Rubric answer: where else could it hide?

The same defect could hide in an HTTP endpoint that performs an action without checking authorization, a BLE writable characteristic without pairing/permissions, a debug Serial command left enabled in production, a WebSocket control message, or an OTA endpoint that accepts an update from any connected client.

## Important limitation

A shared command token is a classroom-sized repair, not the strongest production design. A captured valid command could be replayed. A production actuator should verify a digital signature or HMAC over the command, device ID, sequence number/nonce, and expiration time, and should reject duplicate or expired commands.

## BH1750 note

The starter defines opcode `0x13` but labels it as high-resolution mode. In the BH1750 datasheet, `0x13` is continuous low-resolution mode. This is a separate sensor-code inconsistency, not the planted security defect. Do not confuse it with the security rubric item during the check.
