# Rocket League sign-in through Ripple

## Startup config redirect caused sign-in to fail on November 26, 2026

Ripple's local listeners were healthy, but Rocket League could not go online. The experimental config-host redirect sent the game's startup request to `https://config.localhost`. The game logged `RECV: 0`, meaning it received no HTTP response. The logs did not establish the exact TLS or network failure. With the config fetch failing, the game sent `Auth/AuthPlayer` as a bare `POST /` to Ripple's local reverse listener.

The auth status changed as we tried routing fixes. Falling through to upstream HTTP on port 80 produced a 502 or redirect. Sending the same request to the HTTPS root produced a 404. Forcing the usual auth path on that request produced a 500. All three were downstream of the failed config fetch.

The confirmed fix removed the config-host redirect and the guessed auth-path rewrite. Rocket League fetched its startup config from `https://config.psynet.gg`, then supplied its own `/Auth/AuthPlayer/v2` path. Ripple forwarded it as `/rpc/Auth/AuthPlayer/v2` over HTTPS. The next live launch logged both `SetAuthorized bAuthorized=True` and `PsyNetMessengerWebSocket_X_0 Connected`.

## How to recognize a recurrence

1. Open `Documents\My Games\Rocket League\TAGame\Logs\Launch.log` and find the startup config request in the latest launch. A request to `config.localhost` followed by `RECV: 0` matches this incident. Compare the hostname and status only. Redact query strings before sharing log excerpts.
2. Check `%APPDATA%\RippleRL\proxy_lifecycle.log` for the reverse and forward listener readiness messages. If the game reports `Connection refused` on `49320` or `49321`, investigate listener availability first. If both listeners are ready, use `proxy_trace.log` to compare the request path and HTTP status.
3. Check that the deployed `dist/ripple/proxy_addon.py` matches the current source and that no addon startup task redirects the game's config host. Run `tools\build_ui.bat` from the checkout to publish the current build and modules to `dist`.
4. Close Rocket League and Ripple, then launch Ripple from `dist` and start a fresh game. Verify the config request uses `config.psynet.gg`, the game generates a full auth path, and both authorization and the PsyNet WebSocket succeed. A local readiness probe checks the proxy listeners. The game log verifies the full sign-in path.
