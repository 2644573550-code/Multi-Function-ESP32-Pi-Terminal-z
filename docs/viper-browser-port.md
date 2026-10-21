# Viper Browser integration

Viper-Browser is vendored at `third_party/Viper-Browser/` from upstream commit
`c1738786bfb97bcd5fb6e216c86a673bc4f91a36` (GPL-3.0). The terminal uses its
core request filtering, settings, bookmark/history stores and custom URL
handlers with an embedded Qt Quick `WebEngineView`; it does not embed Viper's
QWidget browser window.

The terminal browser intentionally presents a basic touch-first experience:
navigation, back/forward/reload, tabs, zoom, bookmarks, history, downloads,
PDF export, persistent cookies, ordinary browser preferences,
HTTP authentication and site-permission prompts. The default home page is
`https://baidu.com`. Existing user-selected home pages remain unchanged;
former built-in Startpage/Bing defaults migrate to Baidu. Viper's unsupported
user-script and custom User-Agent settings are forced off for the terminal
profile, and the optional Viper page-global script bundle is not installed.
The page-find action is not part of the basic-browser interface.

The following features have deliberately been removed from the terminal UI and
Quick-page integration: custom User-Agent selection, Netscape bookmark HTML
import/export, user-script installation/toggling/editing, Viper's WebChannel
bridge, and DOM source inspection. Credential save/autofill is also disabled;
the optional libsecret/KWallet providers are not built or required.

Viper's ABP/uBlock network filter engine remains active, with basic subscription
add/update/enable/remove controls. Cosmetic filter data is applied to the
active Qt Quick page after navigation starts. Private tabs use an
off-the-record profile. The terminal's minimal session helper saves ordinary
tab URLs, but does not implement Viper's complete per-tab history, pinning,
hibernation or multi-window session model. Download controls cover the current
run only; history is not persisted.

Downloads from regular and private tabs are accepted synchronously from Qt's
`downloadRequested` signal, as Qt 6.4 destroys unaccepted QML requests when the
signal handler returns. Files use the configured writable download directory
and a sanitized suggested name; the downloads manager opens to show progress and
completion. The current interface does not ask for a per-download filename.

## Verification

```sh
source pi_terminal/scripts/env.sh
cmake -S pi_terminal -B /tmp/opencode/terminal-viper-dev \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DTERMINAL_REPAIR_AUDIT=ON
cmake --build /tmp/opencode/terminal-viper-dev --parallel 4
```

Host build and QML compilation are the current verification evidence. The
long-term GUI audit covers profile attachment, request filtering/custom schemes,
bookmark add/list/remove, menu geometry and synthesized touch scrolling. The
browser-settings/dialog interactions, download transfer, and private-profile
isolation still need dedicated runtime assertions. Host build is not Pi ARM64
or physical-touch acceptance; Pi deployment is reported separately.
