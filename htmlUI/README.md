# FlightEnv htmlUI prototype

This prototype uses React JSX files loaded by Babel in the browser.

Do not open `index.html` directly with `file://`. Browser security rules block Babel from loading local `.jsx` files through XHR/fetch.

Use one of these instead:

```powershell
.\start_html_ui.cmd
```

or:

```powershell
python -m http.server 5177 --bind 127.0.0.1
```

Then open:

```text
http://127.0.0.1:5177/index.html
```

The page also loads React and Babel from unpkg, so it needs network access unless those libraries are bundled locally.
