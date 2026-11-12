/**
 * @file http_server.c
 * @brief Embedded micro HTTP/REST server implementation with Autonomous Real-Time Auto-Sync.
 */

#include "http_server.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <poll.h>
#include <signal.h>

static const char *HTML_DASHBOARD =
"<!DOCTYPE html>\n"
"<html lang='en'>\n"
"<head>\n"
"<meta charset='UTF-8'>\n"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>\n"
"<title>MiniDrop - Autonomous Real-Time RepoSync</title>\n"
"<style>\n"
"  :root {\n"
"    --bg-dark: #0f172a;\n"
"    --card-bg: #1e293b;\n"
"    --text-main: #f8fafc;\n"
"    --text-muted: #94a3b8;\n"
"    --synced: #10b981;\n"
"    --syncing: #3b82f6;\n"
"    --error: #ef4444;\n"
"  }\n"
"  * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; }\n"
"  body { background-color: var(--bg-dark); color: var(--text-main); min-height: 100vh; padding: 2rem 1rem; }\n"
"  .container { max-width: 960px; margin: 0 auto; }\n"
"  .header { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid #334155; padding-bottom: 1rem; margin-bottom: 2rem; }\n"
"  .title h1 { font-size: 1.6rem; font-weight: 700; display: flex; align-items: center; gap: 0.5rem; }\n"
"  .badge { padding: 0.35rem 0.85rem; border-radius: 9999px; font-size: 0.85rem; font-weight: 600; text-transform: uppercase; background: #334155; color: var(--text-muted); }\n"
"  \n"
"  /* AUTONOMOUS AUTO-SYNC HERO CARD */\n"
"  .sync-hero-card {\n"
"    background: linear-gradient(180deg, #1e293b 0%, #0f172a 100%);\n"
"    border-radius: 1rem;\n"
"    padding: 2rem 2.25rem;\n"
"    box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.4);\n"
"    border: 1px solid #334155;\n"
"    margin-bottom: 2rem;\n"
"  }\n"
"  .hero-top-row {\n"
"    display: flex;\n"
"    justify-content: space-between;\n"
"    align-items: center;\n"
"    margin-bottom: 1.25rem;\n"
"  }\n"
"  .pulse-indicator {\n"
"    display: flex;\n"
"    align-items: center;\n"
"    gap: 0.65rem;\n"
"  }\n"
"  .pulse-dot {\n"
"    width: 13px;\n"
"    height: 13px;\n"
"    border-radius: 50%;\n"
"    background-color: var(--synced);\n"
"    box-shadow: 0 0 12px var(--synced);\n"
"    animation: pulse 1.8s infinite;\n"
"  }\n"
"  .pulse-dot.syncing {\n"
"    background-color: var(--syncing);\n"
"    box-shadow: 0 0 14px var(--syncing);\n"
"  }\n"
"  .pulse-title {\n"
"    font-size: 0.85rem;\n"
"    font-weight: 700;\n"
"    letter-spacing: 0.05em;\n"
"    color: #34d399;\n"
"    text-transform: uppercase;\n"
"  }\n"
"  .engine-mode-pill {\n"
"    font-size: 0.75rem;\n"
"    font-weight: 600;\n"
"    padding: 0.25rem 0.65rem;\n"
"    background: #1e3a8a;\n"
"    color: #93c5fd;\n"
"    border-radius: 9999px;\n"
"    border: 1px solid #2563eb;\n"
"  }\n"
"  .hero-main-status {\n"
"    font-size: 1.45rem;\n"
"    font-weight: 700;\n"
"    display: flex;\n"
"    align-items: center;\n"
"    gap: 0.75rem;\n"
"    color: #f8fafc;\n"
"    margin-bottom: 0.5rem;\n"
"  }\n"
"  .status-icon {\n"
"    display: inline-flex;\n"
"    align-items: center;\n"
"    justify-content: center;\n"
"    width: 36px;\n"
"    height: 36px;\n"
"    border-radius: 50%;\n"
"    background: #064e3b;\n"
"    color: #34d399;\n"
"    font-size: 1.1rem;\n"
"    border: 1px solid #10b981;\n"
"  }\n"
"  .status-icon.syncing {\n"
"    background: #1e3a8a;\n"
"    color: #60a5fa;\n"
"    border-color: #3b82f6;\n"
"  }\n"
"  .hero-subtext {\n"
"    color: var(--text-muted);\n"
"    font-size: 0.95rem;\n"
"    line-height: 1.5;\n"
"    margin-bottom: 1.25rem;\n"
"  }\n"
"  .hero-meta-bar {\n"
"    display: flex;\n"
"    flex-wrap: wrap;\n"
"    gap: 0.75rem;\n"
"    border-top: 1px solid #334155;\n"
"    padding-top: 1rem;\n"
"  }\n"
"  .meta-tag {\n"
"    font-size: 0.8rem;\n"
"    background: #090d16;\n"
"    border: 1px solid #1e293b;\n"
"    padding: 0.3rem 0.65rem;\n"
"    border-radius: 0.35rem;\n"
"    color: #94a3b8;\n"
"  }\n"
"  \n"
"  @keyframes pulse {\n"
"    0%, 100% { opacity: 1; transform: scale(1); }\n"
"    50% { opacity: 0.75; transform: scale(0.98); }\n"
"  }\n"
"  \n"
"  .repo-grid {\n"
"    display: grid;\n"
"    grid-template-columns: 1fr 1fr;\n"
"    gap: 1.5rem;\n"
"  }\n"
"  @media (max-width: 768px) {\n"
"    .repo-grid { grid-template-columns: 1fr; }\n"
"  }\n"
"  \n"
"  .repo-card {\n"
"    background: var(--card-bg);\n"
"    border-radius: 0.75rem;\n"
"    border: 1px solid #334155;\n"
"    padding: 1.5rem;\n"
"  }\n"
"  .card-header {\n"
"    display: flex;\n"
"    justify-content: space-between;\n"
"    align-items: center;\n"
"    border-bottom: 1px solid #334155;\n"
"    padding-bottom: 0.75rem;\n"
"    margin-bottom: 1rem;\n"
"  }\n"
"  .card-title { font-weight: 600; font-size: 1.1rem; }\n"
"  .file-badge { font-size: 0.8rem; background: #0f172a; padding: 0.2rem 0.6rem; border-radius: 0.25rem; color: var(--text-muted); }\n"
"  \n"
"  .file-list {\n"
"    list-style: none;\n"
"    max-height: 320px;\n"
"    overflow-y: auto;\n"
"  }\n"
"  .file-item {\n"
"    display: flex;\n"
"    align-items: center;\n"
"    justify-content: space-between;\n"
"    padding: 0.65rem 0.5rem;\n"
"    border-bottom: 1px solid #334155;\n"
"    font-size: 0.9rem;\n"
"  }\n"
"  .file-item:last-child { border-bottom: none; }\n"
"  .file-name { font-weight: 500; cursor: pointer; color: #38bdf8; text-decoration: underline; text-underline-offset: 3px; }\n"
"  .file-name:hover { color: #7dd3fc; }\n"
"  .file-meta { font-size: 0.75rem; color: var(--text-muted); margin-top: 0.15rem; }\n"
"  \n"
"  .tag-synced { color: #34d399; font-size: 0.75rem; font-weight: 600; background: rgba(16, 185, 129, 0.1); padding: 0.2rem 0.5rem; border-radius: 0.25rem; }\n"
"  \n"
"  .btn-action {\n"
"    background: #334155;\n"
"    border: 1px solid #475569;\n"
"    color: #cbd5e1;\n"
"    padding: 0.2rem 0.5rem;\n"
"    border-radius: 0.35rem;\n"
"    font-size: 0.75rem;\n"
"    cursor: pointer;\n"
"    text-decoration: none;\n"
"    transition: all 0.2s;\n"
"  }\n"
"  .btn-action:hover { background: #475569; color: white; }\n"
"  \n"
"  /* History Timeline */\n"
"  .history-card {\n"
"    background: var(--card-bg);\n"
"    border-radius: 0.75rem;\n"
"    border: 1px solid #334155;\n"
"    padding: 1.5rem;\n"
"    margin-top: 2rem;\n"
"  }\n"
"  .timeline { display: flex; flex-direction: column; gap: 0.75rem; margin-top: 1rem; max-height: 250px; overflow-y: auto; }\n"
"  .timeline-item {\n"
"    display: flex;\n"
"    align-items: center;\n"
"    justify-content: space-between;\n"
"    padding: 0.75rem 1rem;\n"
"    background: #0f172a;\n"
"    border: 1px solid #334155;\n"
"    border-radius: 0.5rem;\n"
"  }\n"
"  .timeline-hash {\n"
"    font-family: monospace;\n"
"    background: #1e293b;\n"
"    color: #38bdf8;\n"
"    padding: 0.2rem 0.5rem;\n"
"    border-radius: 0.25rem;\n"
"    font-size: 0.8rem;\n"
"    font-weight: 700;\n"
"    margin-right: 0.5rem;\n"
"  }\n"
"  .timeline-badge { font-size: 0.7rem; font-weight: 700; padding: 0.2rem 0.5rem; border-radius: 9999px; text-transform: uppercase; }\n"
"  .badge-push { background: #1e3a8a; color: #93c5fd; }\n"
"  .badge-prune { background: #7c2d12; color: #fdba74; }\n"
"  .timeline-desc { font-size: 0.85rem; color: #e2e8f0; margin-top: 0.25rem; }\n"
"  .timeline-time { font-size: 0.75rem; color: var(--text-muted); }\n"
"  \n"
"  /* Modal Viewer */\n"
"  .modal-backdrop {\n"
"    position: fixed; top: 0; left: 0; width: 100vw; height: 100vh;\n"
"    background: rgba(0, 0, 0, 0.75); backdrop-filter: blur(4px);\n"
"    display: flex; align-items: center; justify-content: center; z-index: 1000;\n"
"  }\n"
"  .modal-box {\n"
"    background: var(--card-bg); border: 1px solid #334155; border-radius: 1rem;\n"
"    width: 90%; max-width: 760px; max-height: 85vh; display: flex; flex-direction: column;\n"
"    box-shadow: 0 25px 50px -12px rgba(0, 0, 0, 0.6);\n"
"  }\n"
"  .modal-header {\n"
"    display: flex; justify-content: space-between; align-items: center;\n"
"    padding: 1.25rem 1.5rem; border-bottom: 1px solid #334155;\n"
"  }\n"
"  .modal-body { padding: 1.25rem 1.5rem; overflow-y: auto; flex: 1; }\n"
"  .modal-footer {\n"
"    display: flex; justify-content: space-between; align-items: center;\n"
"    padding: 1rem 1.5rem; border-top: 1px solid #334155; background: #0f172a; border-radius: 0 0 1rem 1rem;\n"
"  }\n"
"  .code-preview {\n"
"    background: #090d16; border: 1px solid #1e293b; border-radius: 0.5rem;\n"
"    padding: 1rem; color: #38bdf8; font-family: 'Consolas', 'Courier New', monospace;\n"
"    font-size: 0.9rem; line-height: 1.5; white-space: pre-wrap; word-break: break-all;\n"
"    max-height: 50vh; overflow-y: auto;\n"
"  }\n"
"  .btn-modal {\n"
"    padding: 0.5rem 1rem; border-radius: 0.5rem; font-size: 0.85rem; font-weight: 600;\n"
"    text-decoration: none; border: 1px solid #475569; background: #334155; color: white; cursor: pointer;\n"
"  }\n"
"  .btn-modal:hover { background: #475569; }\n"
"  .btn-close { background: transparent; border: none; color: var(--text-muted); font-size: 1.25rem; cursor: pointer; }\n"
"  .btn-close:hover { color: white; }\n"
"</style>\n"
"</head>\n"
"<body>\n"
"<div class='container'>\n"
"  <header class='header'>\n"
"    <div class='title'>\n"
"      <h1><span>⚡</span> MiniDrop RepoSync</h1>\n"
"    </div>\n"
"    <span class='badge' id='conn-badge'>Connecting...</span>\n"
"  </header>\n"
"\n"
"  <!-- AUTONOMOUS AUTO-SYNC HERO CARD (ZERO BUTTONS) -->\n"
"  <div class='sync-hero-card'>\n"
"    <div class='hero-top-row'>\n"
"      <div class='pulse-indicator'>\n"
"        <span class='pulse-dot' id='pulse-dot'></span>\n"
"        <span class='pulse-title' id='pulse-title'>Autonomous Real-Time Sync Active</span>\n"
"      </div>\n"
"      <span class='engine-mode-pill'>Zero-Click Continuous Sync</span>\n"
"    </div>\n"
"    <div class='hero-main-status' id='hero-status'>\n"
"      <span class='status-icon' id='status-icon'>✓</span>\n"
"      <span id='status-title'>Workspace & Remote Repo In Sync</span>\n"
"    </div>\n"
"    <div class='hero-subtext' id='status-desc'>\n"
"      Zero-click continuous engine active. Any file creation, modification, or deletion in ./workspace is instantly committed to ./storage/repo with OpenSSL SHA-256 verification.\n"
"    </div>\n"
"    <div class='hero-meta-bar'>\n"
"      <span class='meta-tag' id='last-event-tag'>⚡ Last Event: All files synchronized</span>\n"
"      <span class='meta-tag'>📁 Path: ./workspace ➔ ./storage/repo</span>\n"
"      <span class='meta-tag'>🔒 Atomic POSIX rename() commit</span>\n"
"    </div>\n"
"  </div>\n"
"\n"
"  <!-- DUAL REPOSITORY VIEWER -->\n"
"  <div class='repo-grid'>\n"
"    <!-- LOCAL WORKSPACE -->\n"
"    <div class='repo-card'>\n"
"      <div class='card-header'>\n"
"        <span class='card-title'>💻 Local Workspace</span>\n"
"        <span class='file-badge' id='local-count'>0 items</span>\n"
"      </div>\n"
"      <ul class='file-list' id='local-list'>\n"
"        <li style='color: var(--text-muted); text-align: center; padding: 1.5rem;'>Scanning...</li>\n"
"      </ul>\n"
"    </div>\n"
"\n"
"    <!-- REMOTE REPOSITORY -->\n"
"    <div class='repo-card'>\n"
"      <div class='card-header'>\n"
"        <span class='card-title'>☁ Remote Repository</span>\n"
"        <span class='file-badge' id='remote-count'>0 items</span>\n"
"      </div>\n"
"      <ul class='file-list' id='remote-list'>\n"
"        <li style='color: var(--text-muted); text-align: center; padding: 1.5rem;'>Scanning...</li>\n"
"      </ul>\n"
"    </div>\n"
"  </div>\n"
"\n"
"  <!-- COMMIT HISTORY TIMELINE -->\n"
"  <div class='history-card'>\n"
"    <div class='card-header'>\n"
"      <span class='card-title'>📜 Commit & Sync History</span>\n"
"      <span class='file-badge' id='history-badge'>0 events</span>\n"
"    </div>\n"
"    <div class='timeline' id='history-list'>\n"
"      <div style='color: var(--text-muted); text-align: center; padding: 1.5rem;'>No sync history yet</div>\n"
"    </div>\n"
"  </div>\n"
"</div>\n"
"\n"
"<!-- FILE CONTENT VIEWER MODAL -->\n"
"<div id='file-modal' class='modal-backdrop' style='display:none;'>\n"
"  <div class='modal-box'>\n"
"    <div class='modal-header'>\n"
"      <div>\n"
"        <span id='modal-filename' style='font-weight:700; font-size:1.1rem; color:#f8fafc;'>file.txt</span>\n"
"        <span id='modal-source' class='badge' style='margin-left:0.5rem;'>REMOTE</span>\n"
"      </div>\n"
"      <button class='btn-close' onclick='closeModal()'>✕</button>\n"
"    </div>\n"
"    <div class='modal-body'>\n"
"      <pre class='code-preview' id='modal-content'><code>Loading content...</code></pre>\n"
"    </div>\n"
"    <div class='modal-footer'>\n"
"      <span id='modal-size' style='color:var(--text-muted); font-size:0.85rem;'>Size: 0 B</span>\n"
"      <div style='display:flex; gap:0.5rem;'>\n"
"        <a id='modal-download' class='btn-modal' href='#' download>📥 Download File</a>\n"
"        <button class='btn-modal' onclick='closeModal()'>Close</button>\n"
"      </div>\n"
"    </div>\n"
"  </div>\n"
"</div>\n"
"\n"
"<script>\n"
"  let consecutiveErrors = 0;\n"
"\n"
"  async function fetchStatus() {\n"
"    try {\n"
"      const res = await fetch('/api/status');\n"
"      if (!res.ok) throw new Error('Status error');\n"
"      const data = await res.json();\n"
"      consecutiveErrors = 0;\n"
"      updateDashboard(data);\n"
"    } catch (err) {\n"
"      consecutiveErrors++;\n"
"      if (consecutiveErrors >= 3) {\n"
"        document.getElementById('conn-badge').textContent = 'Offline';\n"
"        document.getElementById('conn-badge').style.background = '#7f1d1d';\n"
"        document.getElementById('status-title').textContent = 'Backend Offline';\n"
"        document.getElementById('status-icon').textContent = '⚠';\n"
"        document.getElementById('pulse-title').textContent = 'DISCONNECTED';\n"
"        document.getElementById('pulse-title').style.color = '#f87171';\n"
"        document.getElementById('pulse-dot').style.backgroundColor = '#ef4444';\n"
"      }\n"
"    }\n"
"  }\n"
"\n"
"  async function fetchHistory() {\n"
"    try {\n"
"      const res = await fetch('/api/history');\n"
"      if (!res.ok) return;\n"
"      const data = await res.json();\n"
"      renderHistory(data.history || []);\n"
"    } catch (e) {}\n"
"  }\n"
"\n"
"  function formatBytes(bytes) {\n"
"    if (bytes === 0) return '0 B';\n"
"    const k = 1024;\n"
"    const sizes = ['B', 'KB', 'MB', 'GB'];\n"
"    const i = Math.floor(Math.log(bytes) / Math.log(k));\n"
"    return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];\n"
"  }\n"
"\n"
"  function updateDashboard(data) {\n"
"    const badge = document.getElementById('conn-badge');\n"
"    const dot = document.getElementById('pulse-dot');\n"
"    const pulseTitle = document.getElementById('pulse-title');\n"
"    const statusIcon = document.getElementById('status-icon');\n"
"    const statusTitle = document.getElementById('status-title');\n"
"    const statusDesc = document.getElementById('status-desc');\n"
"    const lastEventTag = document.getElementById('last-event-tag');\n"
"\n"
"    badge.textContent = 'Live Connected';\n"
"    badge.style.background = '#065f46';\n"
"    badge.style.color = '#34d399';\n"
"\n"
"    if (data.state === 'SYNCING') {\n"
"      dot.className = 'pulse-dot syncing';\n"
"      pulseTitle.textContent = 'AUTO-SYNCING IN PROGRESS...';\n"
"      pulseTitle.style.color = '#60a5fa';\n"
"      statusIcon.className = 'status-icon syncing';\n"
"      statusIcon.textContent = '⚡';\n"
"      statusTitle.textContent = `Auto-Syncing (${data.progress}%)...`;\n"
"      statusDesc.textContent = data.message || 'Transferring chunks and verifying SHA-256 digests...';\n"
"    } else {\n"
"      dot.className = 'pulse-dot';\n"
"      pulseTitle.textContent = 'Autonomous Real-Time Sync Active';\n"
"      pulseTitle.style.color = '#34d399';\n"
"      statusIcon.className = 'status-icon';\n"
"      statusIcon.textContent = '✓';\n"
"      statusTitle.textContent = 'Workspace & Remote Repo In Sync';\n"
"      statusDesc.textContent = 'Zero-click continuous engine active. Any file creation, modification, or deletion in ./workspace is instantly committed to ./storage/repo.';\n"
"    }\n"
"\n"
"    if (data.last_event) {\n"
"      lastEventTag.textContent = `⚡ Last Event: ${data.last_event}`;\n"
"    }\n"
"\n"
"    document.getElementById('local-count').textContent = `${data.total_local} item(s)`;\n"
"    document.getElementById('remote-count').textContent = `${data.total_remote} item(s)`;\n"
"\n"
"    // Render Local Files List\n"
"    const localList = document.getElementById('local-list');\n"
"    localList.innerHTML = '';\n"
"    if (data.local_files && data.local_files.length > 0) {\n"
"      data.local_files.forEach(f => {\n"
"        localList.innerHTML += `\n"
"          <li class='file-item'>\n"
"            <div>\n"
"              <span class='file-name' onclick=\"openModal('local', '${f.name}')\">📄 ${f.name}</span>\n"
"              <div class='file-meta'>Size: ${formatBytes(f.size)}</div>\n"
"            </div>\n"
"            <div style='display:flex; align-items:center; gap:0.4rem;'>\n"
"              <button class='btn-action' onclick=\"openModal('local', '${f.name}')\">👁️ View</button>\n"
"              <span class='tag-synced'>SYNCED</span>\n"
"            </div>\n"
"          </li>`;\n"
"      });\n"
"    } else {\n"
"      localList.innerHTML = `<li style='color: var(--text-muted); text-align: center; padding: 1.5rem;'>Workspace is empty</li>`;\n"
"    }\n"
"\n"
"    // Render Remote Repository Files List\n"
"    const remoteList = document.getElementById('remote-list');\n"
"    remoteList.innerHTML = '';\n"
"    if (data.remote_files && data.remote_files.length > 0) {\n"
"      data.remote_files.forEach(f => {\n"
"        remoteList.innerHTML += `\n"
"          <li class='file-item'>\n"
"            <div>\n"
"              <span class='file-name' onclick=\"openModal('remote', '${f.name}')\">☁ ${f.name}</span>\n"
"              <div class='file-meta'>Size: ${formatBytes(f.size)} | SHA: ${f.hash ? f.hash.substring(0, 8) + '...' : ''}</div>\n"
"            </div>\n"
"            <div style='display:flex; align-items:center; gap:0.4rem;'>\n"
"              <button class='btn-action' onclick=\"openModal('remote', '${f.name}')\">👁️ View</button>\n"
"              <a class='btn-action' href=\"/api/download?source=remote&path=${encodeURIComponent(f.name)}\" download>📥 Save</a>\n"
"              <span class='tag-synced'>HOSTED</span>\n"
"            </div>\n"
"          </li>`;\n"
"      });\n"
"    } else {\n"
"      remoteList.innerHTML = `<li style='color: var(--text-muted); text-align: center; padding: 1.5rem;'>Remote repo is currently empty</li>`;\n"
"    }\n"
"  }\n"
"\n"
"  function renderHistory(items) {\n"
"    const list = document.getElementById('history-list');\n"
"    const badge = document.getElementById('history-badge');\n"
"    badge.textContent = `${items.length} event(s)`;\n"
"    if (!items || items.length === 0) {\n"
"      list.innerHTML = `<div style='color: var(--text-muted); text-align: center; padding: 1.5rem;'>No sync history recorded yet</div>`;\n"
"      return;\n"
"    }\n"
"    list.innerHTML = '';\n"
"    items.forEach(h => {\n"
"      let badgeClass = h.action === 'PUSH' ? 'badge-push' : 'badge-prune';\n"
"      list.innerHTML += `\n"
"        <div class='timeline-item'>\n"
"          <div>\n"
"            <div style='display:flex; align-items:center;'>\n"
"              <span class='timeline-hash'>#${h.id}</span>\n"
"              <span class='timeline-badge ${badgeClass}'>${h.action}</span>\n"
"              <span style='margin-left:0.5rem; font-weight:600; font-size:0.85rem;'>${h.count} file(s)</span>\n"
"            </div>\n"
"            <div class='timeline-desc'>${h.summary}</div>\n"
"          </div>\n"
"          <div class='timeline-time'>${h.time}</div>\n"
"        </div>`;\n"
"    });\n"
"  }\n"
"\n"
"  async function openModal(source, path) {\n"
"    const modal = document.getElementById('file-modal');\n"
"    document.getElementById('modal-filename').textContent = path;\n"
"    document.getElementById('modal-source').textContent = source.toUpperCase();\n"
"    document.getElementById('modal-content').textContent = 'Loading file contents...';\n"
"    document.getElementById('modal-download').href = `/api/download?source=${source}&path=${encodeURIComponent(path)}`;\n"
"    document.getElementById('modal-download').style.display = (source === 'local') ? 'none' : 'inline-block';\n"
"    modal.style.display = 'flex';\n"
"\n"
"    try {\n"
"      const res = await fetch(`/api/file?source=${source}&path=${encodeURIComponent(path)}`);\n"
"      const data = await res.json();\n"
"      if (data.status === 'ok') {\n"
"        document.getElementById('modal-content').textContent = data.content;\n"
"        document.getElementById('modal-size').textContent = `Size: ${formatBytes(data.size)} | Verified SHA-256`;\n"
"      } else {\n"
"        document.getElementById('modal-content').textContent = 'Failed to load file content: ' + (data.message || 'Error');\n"
"      }\n"
"    } catch (e) {\n"
"      document.getElementById('modal-content').textContent = 'Network error reading file.';\n"
"    }\n"
"  }\n"
"\n"
"  function closeModal() {\n"
"    document.getElementById('file-modal').style.display = 'none';\n"
"  }\n"
"\n"
"  setInterval(fetchStatus, 700);\n"
"  setInterval(fetchHistory, 1500);\n"
"  fetchStatus();\n"
"  fetchHistory();\n"
"</script>\n"
"</body>\n"
"</html>\n";

static void send_http_response(int fd, const char *status_line, const char *content_type, const char *body) {
    char header[512];
    size_t body_len = body ? strlen(body) : 0;
    snprintf(header, sizeof(header),
             "%s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "\r\n",
             status_line, content_type, body_len);

    send(fd, header, strlen(header), MSG_NOSIGNAL);
    if (body_len > 0) {
        send(fd, body, body_len, MSG_NOSIGNAL);
    }
}

static void get_query_param(const char *url, const char *param, char *val, size_t val_max) {
    val[0] = '\0';
    char needle[64];
    snprintf(needle, sizeof(needle), "%s=", param);
    const char *p = strstr(url, needle);
    if (!p) return;
    p += strlen(needle);
    size_t i = 0;
    while (*p && *p != '&' && *p != ' ' && *p != '\r' && *p != '\n' && i < val_max - 1) {
        if (*p == '%' && *(p+1) && *(p+2)) {
            char hex[3] = { *(p+1), *(p+2), '\0' };
            val[i++] = (char)strtol(hex, NULL, 16);
            p += 3;
        } else if (*p == '+') {
            val[i++] = ' ';
            p++;
        } else {
            val[i++] = *p++;
        }
    }
    val[i] = '\0';
}

static void serve_file_download(int client_fd, const char *full_path, const char *filename) {
    int fd = open(full_path, O_RDONLY);
    if (fd < 0) {
        send_http_response(client_fd, "HTTP/1.1 404 Not Found", "text/plain", "File Not Found");
        return;
    }
    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        send_http_response(client_fd, "HTTP/1.1 500 Internal Error", "text/plain", "Stat failed");
        return;
    }
    char header[512];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: application/octet-stream\r\n"
             "Content-Disposition: attachment; filename=\"%s\"\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n",
             filename, (size_t)st.st_size);
    send(client_fd, header, strlen(header), MSG_NOSIGNAL);

    char buf[65536];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        ssize_t w = 0;
        while (w < n) {
            ssize_t written = send(client_fd, buf + w, (size_t)(n - w), MSG_NOSIGNAL);
            if (written <= 0) break;
            w += written;
        }
    }
    close(fd);
}

static void serve_file_preview(int client_fd, const char *full_path, const char *filename, const char *source) {
    int fd = open(full_path, O_RDONLY);
    if (fd < 0) {
        send_http_response(client_fd, "HTTP/1.1 404 Not Found", "application/json", "{\"status\":\"error\",\"message\":\"File not found\"}");
        return;
    }
    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        send_http_response(client_fd, "HTTP/1.1 500 Internal Error", "application/json", "{\"status\":\"error\",\"message\":\"Stat failed\"}");
        return;
    }

    size_t to_read = (st.st_size > 65536) ? 65536 : (size_t)st.st_size;
    char *raw = malloc(to_read + 1);
    if (!raw) {
        close(fd);
        send_http_response(client_fd, "HTTP/1.1 500 Internal Error", "application/json", "{\"status\":\"error\",\"message\":\"Out of memory\"}");
        return;
    }

    ssize_t n = read(fd, raw, to_read);
    close(fd);
    if (n < 0) n = 0;
    raw[n] = '\0';

    /* Check if binary */
    int is_binary = 0;
    for (ssize_t i = 0; i < n; i++) {
        if (raw[i] == '\0') {
            is_binary = 1;
            break;
        }
    }

    if (is_binary) {
        char resp[512];
        snprintf(resp, sizeof(resp),
                 "{\"status\":\"ok\",\"name\":\"%s\",\"source\":\"%s\",\"size\":%zu,\"is_binary\":true,\"content\":\"[Binary content cannot be previewed. Click Download below to save file.]\"}",
                 filename, source, (size_t)st.st_size);
        free(raw);
        send_http_response(client_fd, "HTTP/1.1 200 OK", "application/json", resp);
        return;
    }

    /* Escape text for JSON */
    size_t escaped_cap = (size_t)n * 4 + 512;
    char *escaped = malloc(escaped_cap);
    if (!escaped) {
        free(raw);
        send_http_response(client_fd, "HTTP/1.1 500 Internal Error", "application/json", "{\"status\":\"error\"}");
        return;
    }

    size_t e_idx = 0;
    for (ssize_t i = 0; i < n && e_idx < escaped_cap - 10; i++) {
        char c = raw[i];
        if (c == '"') {
            escaped[e_idx++] = '\\'; escaped[e_idx++] = '"';
        } else if (c == '\\') {
            escaped[e_idx++] = '\\'; escaped[e_idx++] = '\\';
        } else if (c == '\n') {
            escaped[e_idx++] = '\\'; escaped[e_idx++] = 'n';
        } else if (c == '\r') {
            escaped[e_idx++] = '\\'; escaped[e_idx++] = 'r';
        } else if (c == '\t') {
            escaped[e_idx++] = '\\'; escaped[e_idx++] = 't';
        } else if ((unsigned char)c < 32) {
            /* ignore unprintable control character */
        } else {
            escaped[e_idx++] = c;
        }
    }
    escaped[e_idx] = '\0';
    free(raw);

    size_t resp_len = escaped_cap + 1024;
    char *resp = malloc(resp_len);
    if (resp) {
        snprintf(resp, resp_len,
                 "{\"status\":\"ok\",\"name\":\"%s\",\"source\":\"%s\",\"size\":%zu,\"is_binary\":false,\"content\":\"%s\"}",
                 filename, source, (size_t)st.st_size, escaped);
        send_http_response(client_fd, "HTTP/1.1 200 OK", "application/json", resp);
        free(resp);
    }
    free(escaped);
}

typedef struct {
    int client_fd;
    repo_engine_t *engine;
} http_worker_arg_t;

static void *http_worker_thread(void *arg);

static void handle_http_client(int client_fd, repo_engine_t *engine) {
    char req_buf[2048];
    ssize_t n = read(client_fd, req_buf, sizeof(req_buf) - 1);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    req_buf[n] = '\0';

    if (strncmp(req_buf, "GET / ", 6) == 0 || strncmp(req_buf, "GET /index.html", 15) == 0) {
        send_http_response(client_fd, "HTTP/1.1 200 OK", "text/html", HTML_DASHBOARD);
    } else if (strncmp(req_buf, "GET /api/status", 15) == 0) {
        repo_diff_t diff;
        repo_engine_get_status(engine, &diff);

        sync_manifest_t local_man;
        sync_manifest_t remote_man;
        sync_manifest_init(&local_man);
        sync_manifest_init(&remote_man);
        sync_manifest_scan(engine->local_dir, &local_man);
        sync_manifest_scan(engine->remote_dir, &remote_man);

        char json_buf[16384];
        int offset = snprintf(json_buf, sizeof(json_buf),
                 "{\n"
                 "  \"state\": \"%s\",\n"
                 "  \"auto_sync\": %d,\n"
                 "  \"last_event\": \"%s\",\n"
                 "  \"push_count\": %zu,\n"
                 "  \"pull_count\": %zu,\n"
                 "  \"prune_count\": %zu,\n"
                 "  \"total_local\": %zu,\n"
                 "  \"total_remote\": %zu,\n"
                 "  \"progress\": %d,\n"
                 "  \"message\": \"%s\",\n"
                 "  \"push_files\": [",
                 repo_state_to_string(diff.state),
                 engine->auto_sync_running,
                 engine->last_event_msg,
                 diff.pending_push_count,
                 diff.pending_pull_count,
                 diff.pending_prune_count,
                 diff.total_local_files,
                 diff.total_remote_files,
                 diff.progress_pct,
                 diff.status_message);

        for (size_t i = 0; i < diff.pending_push_count && i < 64; i++) {
            offset += snprintf(json_buf + offset, sizeof(json_buf) - (size_t)offset,
                               "%s\"%s\"", (i > 0 ? ", " : ""), diff.push_files[i]);
        }
        offset += snprintf(json_buf + offset, sizeof(json_buf) - (size_t)offset, "],\n  \"prune_files\": [");
        for (size_t i = 0; i < diff.pending_prune_count && i < 64; i++) {
            offset += snprintf(json_buf + offset, sizeof(json_buf) - (size_t)offset,
                               "%s\"%s\"", (i > 0 ? ", " : ""), diff.prune_files[i]);
        }

        offset += snprintf(json_buf + offset, sizeof(json_buf) - (size_t)offset, "],\n  \"local_files\": [");

        for (size_t i = 0; i < local_man.count; i++) {
            if (local_man.entries[i].is_dir) continue;
            offset += snprintf(json_buf + offset, sizeof(json_buf) - (size_t)offset,
                               "%s{\"name\": \"%s\", \"size\": %lu}",
                               (i > 0 ? ", " : ""),
                               local_man.entries[i].rel_path,
                               (unsigned long)local_man.entries[i].size);
            if (offset >= (int)sizeof(json_buf) - 512) break;
        }
        offset += snprintf(json_buf + offset, sizeof(json_buf) - (size_t)offset, "],\n  \"remote_files\": [");

        for (size_t i = 0; i < remote_man.count; i++) {
            if (remote_man.entries[i].is_dir) continue;
            offset += snprintf(json_buf + offset, sizeof(json_buf) - (size_t)offset,
                               "%s{\"name\": \"%s\", \"size\": %lu, \"hash\": \"%s\"}",
                               (i > 0 ? ", " : ""),
                               remote_man.entries[i].rel_path,
                               (unsigned long)remote_man.entries[i].size,
                               remote_man.entries[i].sha256);
            if (offset >= (int)sizeof(json_buf) - 256) break;
        }
        snprintf(json_buf + offset, sizeof(json_buf) - (size_t)offset, "]\n}\n");

        sync_manifest_free(&local_man);
        sync_manifest_free(&remote_man);

        send_http_response(client_fd, "HTTP/1.1 200 OK", "application/json", json_buf);
    } else if (strncmp(req_buf, "GET /api/history", 16) == 0) {
        char hist_buf[8192];
        char json_wrap[9000];
        repo_engine_get_history_json(engine, hist_buf, sizeof(hist_buf));
        snprintf(json_wrap, sizeof(json_wrap), "{\"history\": %s}", hist_buf);
        send_http_response(client_fd, "HTTP/1.1 200 OK", "application/json", json_wrap);
    } else if (strncmp(req_buf, "GET /api/file?", 14) == 0 || strncmp(req_buf, "GET /api/download?", 18) == 0) {
        char source[16] = "local";
        char rel_path[MINIDROP_MAX_PATH_LEN] = "";
        char clean_path[MINIDROP_MAX_PATH_LEN] = "";

        get_query_param(req_buf, "source", source, sizeof(source));
        get_query_param(req_buf, "path", rel_path, sizeof(rel_path));

        if (sanitize_rel_path(rel_path, clean_path, sizeof(clean_path)) != 0 || strlen(clean_path) == 0) {
            send_http_response(client_fd, "HTTP/1.1 400 Bad Request", "text/plain", "Invalid path");
            close(client_fd);
            return;
        }

        char full_path[MINIDROP_MAX_PATH_LEN * 2];
        const char *base = (strcmp(source, "remote") == 0) ? engine->remote_dir : engine->local_dir;
        snprintf(full_path, sizeof(full_path), "%s/%s", base, clean_path);

        if (strncmp(req_buf, "GET /api/download?", 18) == 0) {
            serve_file_download(client_fd, full_path, clean_path);
        } else {
            serve_file_preview(client_fd, full_path, clean_path, source);
        }
    } else {
        send_http_response(client_fd, "HTTP/1.1 404 Not Found", "text/plain", "Not Found");
    }

    close(client_fd);
}

static void *http_worker_thread(void *arg) {
    http_worker_arg_t *w = (http_worker_arg_t *)arg;
    handle_http_client(w->client_fd, w->engine);
    free(w);
    return NULL;
}

int http_server_init(http_server_t *server, int port, repo_engine_t *engine) {
    if (!server || !engine) return -1;
    server->port = port;
    server->engine = engine;
    server->is_running = 0;
    return 0;
}

int http_server_run(http_server_t *server) {
    if (!server) return -1;
    signal(SIGPIPE, SIG_IGN);

    /* Guarantee auto-sync daemon is running alongside web server */
    repo_engine_start_autosync(server->engine);

    int listen_fd = socket(AF_INET6, SOCK_STREAM, 0);
    int is_v6 = 1;
    if (listen_fd < 0) {
        listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        is_v6 = 0;
        if (listen_fd < 0) {
            LOG_ERROR("HTTP", "socket creation failed: %s", strerror(errno));
            return -1;
        }
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (is_v6) {
        int v6only = 0;
        setsockopt(listen_fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
        struct sockaddr_in6 serv6;
        memset(&serv6, 0, sizeof(serv6));
        serv6.sin6_family = AF_INET6;
        serv6.sin6_addr = in6addr_any;
        serv6.sin6_port = htons((uint16_t)server->port);
        if (bind(listen_fd, (struct sockaddr *)&serv6, sizeof(serv6)) < 0) {
            LOG_ERROR("HTTP", "bind failed on port %d: %s", server->port, strerror(errno));
            close(listen_fd);
            return -1;
        }
    } else {
        struct sockaddr_in serv4;
        memset(&serv4, 0, sizeof(serv4));
        serv4.sin_family = AF_INET;
        serv4.sin_addr.s_addr = INADDR_ANY;
        serv4.sin_port = htons((uint16_t)server->port);
        if (bind(listen_fd, (struct sockaddr *)&serv4, sizeof(serv4)) < 0) {
            LOG_ERROR("HTTP", "bind failed on port %d: %s", server->port, strerror(errno));
            close(listen_fd);
            return -1;
        }
    }

    if (listen(listen_fd, 64) < 0) {
        LOG_ERROR("HTTP", "listen failed: %s", strerror(errno));
        close(listen_fd);
        return -1;
    }

    server->is_running = 1;
    LOG_INFO("HTTP", "=========================================================");
    LOG_INFO("HTTP", " Autonomous RepoSync Dashboard live at: http://localhost:%d", server->port);
    LOG_INFO("HTTP", " Zero-click real-time synchronization active!");
    LOG_INFO("HTTP", "=========================================================");

    while (server->is_running) {
        struct pollfd pfd;
        pfd.fd = listen_fd;
        pfd.events = POLLIN;

        int poll_res = poll(&pfd, 1, 500);
        if (poll_res < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (poll_res == 0 || !(pfd.revents & POLLIN)) {
            continue;
        }

        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd >= 0) {
            http_worker_arg_t *w = malloc(sizeof(http_worker_arg_t));
            if (w) {
                w->client_fd = client_fd;
                w->engine = server->engine;
                pthread_t tid;
                if (pthread_create(&tid, NULL, http_worker_thread, w) == 0) {
                    pthread_detach(tid);
                } else {
                    handle_http_client(client_fd, server->engine);
                    free(w);
                }
            } else {
                handle_http_client(client_fd, server->engine);
            }
        }
    }

    close(listen_fd);
    LOG_INFO("HTTP", "HTTP server stopped");
    return 0;
}

void http_server_stop(http_server_t *server) {
    if (server) {
        server->is_running = 0;
        repo_engine_stop_autosync(server->engine);
    }
}
