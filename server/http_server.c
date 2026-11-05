/**
 * @file http_server.c
 * @brief Embedded micro HTTP/REST server implementation with complete Remote Repo file listing.
 */

#include "http_server.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <poll.h>

static const char *HTML_DASHBOARD =
"<!DOCTYPE html>\n"
"<html lang='en'>\n"
"<head>\n"
"<meta charset='UTF-8'>\n"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>\n"
"<title>MiniDrop - Single-User Remote Repo</title>\n"
"<style>\n"
"  :root {\n"
"    --bg-dark: #0f172a;\n"
"    --card-bg: #1e293b;\n"
"    --text-main: #f8fafc;\n"
"    --text-muted: #94a3b8;\n"
"    --synced: #10b981;\n"
"    --push: #3b82f6;\n"
"    --pull: #8b5cf6;\n"
"    --syncing: #f59e0b;\n"
"    --error: #ef4444;\n"
"  }\n"
"  * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; }\n"
"  body { background-color: var(--bg-dark); color: var(--text-main); min-height: 100vh; padding: 2rem 1rem; }\n"
"  .container { max-width: 960px; margin: 0 auto; }\n"
"  .header { display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid #334155; padding-bottom: 1rem; margin-bottom: 2rem; }\n"
"  .title h1 { font-size: 1.6rem; font-weight: 700; display: flex; align-items: center; gap: 0.5rem; }\n"
"  .badge { padding: 0.35rem 0.85rem; border-radius: 9999px; font-size: 0.85rem; font-weight: 600; text-transform: uppercase; background: #334155; color: var(--text-muted); }\n"
"  \n"
"  .button-hero-card {\n"
"    background: var(--card-bg);\n"
"    border-radius: 1rem;\n"
"    padding: 2.5rem 2rem;\n"
"    text-align: center;\n"
"    box-shadow: 0 10px 25px -5px rgba(0, 0, 0, 0.3);\n"
"    border: 1px solid #334155;\n"
"    margin-bottom: 2rem;\n"
"  }\n"
"  \n"
"  /* THE SMART DYNAMIC ACTION BUTTON */\n"
"  #smart-btn {\n"
"    width: 100%;\n"
"    max-width: 500px;\n"
"    padding: 1.25rem 2rem;\n"
"    font-size: 1.25rem;\n"
"    font-weight: 700;\n"
"    border: none;\n"
"    border-radius: 0.75rem;\n"
"    cursor: pointer;\n"
"    transition: all 0.3s cubic-bezier(0.4, 0, 0.2, 1);\n"
"    display: inline-flex;\n"
"    align-items: center;\n"
"    justify-content: center;\n"
"    gap: 0.75rem;\n"
"    box-shadow: 0 4px 14px rgba(0,0,0,0.25);\n"
"  }\n"
"  \n"
"  .btn-synced {\n"
"    background: #064e3b;\n"
"    color: #6ee7b7;\n"
"    border: 1px solid var(--synced) !important;\n"
"    cursor: default !important;\n"
"  }\n"
"  .btn-push {\n"
"    background: linear-gradient(135deg, #2563eb, #1d4ed8);\n"
"    color: white;\n"
"    box-shadow: 0 0 25px rgba(59, 130, 246, 0.5) !important;\n"
"  }\n"
"  .btn-push:hover { transform: translateY(-2px); box-shadow: 0 0 35px rgba(59, 130, 246, 0.8) !important; }\n"
"  \n"
"  .btn-pull {\n"
"    background: linear-gradient(135deg, #7c3aed, #6d28d9);\n"
"    color: white;\n"
"    box-shadow: 0 0 25px rgba(139, 92, 246, 0.5) !important;\n"
"  }\n"
"  .btn-pull:hover { transform: translateY(-2px); box-shadow: 0 0 35px rgba(139, 92, 246, 0.8) !important; }\n"
"  \n"
"  .btn-syncing {\n"
"    background: #78350f;\n"
"    color: #fde68a;\n"
"    cursor: not-allowed !important;\n"
"    animation: pulse 1.5s infinite;\n"
"  }\n"
"  \n"
"  .btn-error {\n"
"    background: #7f1d1d;\n"
"    color: #fca5a5;\n"
"  }\n"
"  \n"
"  @keyframes pulse {\n"
"    0%, 100% { opacity: 1; }\n"
"    50% { opacity: 0.6; }\n"
"  }\n"
"  \n"
"  .status-subtitle {\n"
"    margin-top: 1rem;\n"
"    font-size: 0.95rem;\n"
"    color: var(--text-muted);\n"
"  }\n"
"  \n"
"  .grid {\n"
"    display: grid;\n"
"    grid-template-columns: 1fr 1fr;\n"
"    gap: 1.5rem;\n"
"  }\n"
"  @media (max-width: 700px) { .grid { grid-template-columns: 1fr; } }\n"
"  \n"
"  .panel {\n"
"    background: var(--card-bg);\n"
"    border-radius: 0.75rem;\n"
"    padding: 1.5rem;\n"
"    border: 1px solid #334155;\n"
"  }\n"
"  .panel-header {\n"
"    font-size: 1.1rem;\n"
"    font-weight: 600;\n"
"    margin-bottom: 1rem;\n"
"    display: flex;\n"
"    justify-content: space-between;\n"
"    align-items: center;\n"
"    border-bottom: 1px solid #334155;\n"
"    padding-bottom: 0.75rem;\n"
"  }\n"
"  .file-list { list-style: none; max-height: 350px; overflow-y: auto; }\n"
"  .file-item {\n"
"    padding: 0.75rem 1rem;\n"
"    background: #0f172a;\n"
"    margin-bottom: 0.5rem;\n"
"    border-radius: 0.5rem;\n"
"    display: flex;\n"
"    justify-content: space-between;\n"
"    align-items: center;\n"
"    font-size: 0.9rem;\n"
"    border: 1px solid #1e293b;\n"
"  }\n"
"  .file-name { font-weight: 500; display: flex; align-items: center; gap: 0.5rem; }\n"
"  .tag-new { background: rgba(59, 130, 246, 0.15); color: #60a5fa; padding: 0.2rem 0.5rem; border-radius: 0.25rem; font-weight: 600; font-size: 0.75rem; }\n"
"  .tag-synced { background: rgba(16, 185, 129, 0.15); color: #34d399; padding: 0.2rem 0.5rem; border-radius: 0.25rem; font-weight: 600; font-size: 0.75rem; }\n"
"  .file-meta { font-size: 0.75rem; color: var(--text-muted); margin-top: 0.2rem; }\n"
"</style>\n"
"</head>\n"
"<body>\n"
"<div class='container'>\n"
"  <div class='header'>\n"
"    <div class='title'>\n"
"      <h1>📦 MiniDrop Remote Repo</h1>\n"
"    </div>\n"
"    <div>\n"
"      <span id='conn-badge' class='badge'>Connected</span>\n"
"    </div>\n"
"  </div>\n"
"\n"
"  <!-- SMART ACTION BUTTON HERO -->\n"
"  <div class='button-hero-card'>\n"
"    <button id='smart-btn' class='btn-synced' onclick='handleSmartButtonClick()'>\n"
"      <span id='btn-icon'>✓</span>\n"
"      <span id='btn-text'>Checking Status...</span>\n"
"    </button>\n"
"    <div id='status-msg' class='status-subtitle'>Fetching live repository differential...</div>\n"
"  </div>\n"
"\n"
"  <!-- REPOSITORY FILES DISPLAY -->\n"
"  <div class='grid'>\n"
"    <div class='panel'>\n"
"      <div class='panel-header'>\n"
"        <span>💻 Local Workspace</span>\n"
"        <span id='local-count' style='color: var(--text-muted); font-size: 0.9rem;'>0 items</span>\n"
"      </div>\n"
"      <ul id='local-list' class='file-list'>\n"
"        <li style='color: var(--text-muted); text-align: center; padding: 1rem;'>Scanning...</li>\n"
"      </ul>\n"
"    </div>\n"
"\n"
"    <div class='panel'>\n"
"      <div class='panel-header'>\n"
"        <span>☁ Remote Repository</span>\n"
"        <span id='remote-count' style='color: var(--text-muted); font-size: 0.9rem;'>0 items</span>\n"
"      </div>\n"
"      <ul id='remote-list' class='file-list'>\n"
"        <li style='color: var(--text-muted); text-align: center; padding: 1rem;'>Scanning...</li>\n"
"      </ul>\n"
"    </div>\n"
"  </div>\n"
"</div>\n"
"\n"
"<script>\n"
"  let currentState = 'SYNCED';\n"
"\n"
"  async function fetchStatus() {\n"
"    try {\n"
"      const res = await fetch('/api/status');\n"
"      if (!res.ok) throw new Error('Status error');\n"
"      const data = await res.json();\n"
"      updateDashboard(data);\n"
"    } catch (err) {\n"
"      const btn = document.getElementById('smart-btn');\n"
"      btn.className = 'btn-error';\n"
"      document.getElementById('btn-icon').textContent = '⚠';\n"
"      document.getElementById('btn-text').textContent = 'Server Offline';\n"
"      document.getElementById('status-msg').textContent = 'Unable to connect to MiniDrop backend.';\n"
"      document.getElementById('conn-badge').textContent = 'Offline';\n"
"      document.getElementById('conn-badge').style.background = '#7f1d1d';\n"
"    }\n"
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
"    currentState = data.state;\n"
"    const btn = document.getElementById('smart-btn');\n"
"    const btnIcon = document.getElementById('btn-icon');\n"
"    const btnText = document.getElementById('btn-text');\n"
"    const msg = document.getElementById('status-msg');\n"
"    const badge = document.getElementById('conn-badge');\n"
"\n"
"    badge.textContent = 'Live Connected';\n"
"    badge.style.background = '#065f46';\n"
"    badge.style.color = '#34d399';\n"
"\n"
"    if (data.state === 'SYNCED') {\n"
"      btn.className = 'btn-synced';\n"
"      btnIcon.textContent = '✓';\n"
"      btnText.textContent = 'Repository Up to Date';\n"
"      msg.textContent = 'All local files match the remote repository bit-for-bit.';\n"
"    } else if (data.state === 'NEEDS_PUSH') {\n"
"      btn.className = 'btn-push';\n"
"      btnIcon.textContent = '⬆';\n"
"      btnText.textContent = `Push to Remote (${data.push_count} ${data.push_count === 1 ? 'File' : 'Files'})`;\n"
"      msg.textContent = data.message || 'Local modifications detected. Click button to update remote repo.';\n"
"    } else if (data.state === 'NEEDS_PULL') {\n"
"      btn.className = 'btn-pull';\n"
"      btnIcon.textContent = '⬇';\n"
"      btnText.textContent = `Pull from Remote (${data.pull_count} ${data.pull_count === 1 ? 'File' : 'Files'})`;\n"
"      msg.textContent = data.message || 'Remote repository has updates. Click button to pull changes.';\n"
"    } else if (data.state === 'SYNCING') {\n"
"      btn.className = 'btn-syncing';\n"
"      btnIcon.textContent = '⏳';\n"
"      btnText.textContent = `Syncing... (${data.progress}%)`;\n"
"      msg.textContent = data.message || 'Transferring chunks and verifying SHA-256...';\n"
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
"        const isPending = data.push_files && data.push_files.includes(f.name);\n"
"        localList.innerHTML += `\n"
"          <li class='file-item'>\n"
"            <div>\n"
"              <div class='file-name'>📄 ${f.name}</div>\n"
"              <div class='file-meta'>Size: ${formatBytes(f.size)}</div>\n"
"            </div>\n"
"            <span class='${isPending ? \"tag-new\" : \"tag-synced\"}'>${isPending ? \"MODIFIED / NEW\" : \"SYNCED\"}</span>\n"
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
"        localList;\n"
"        remoteList.innerHTML += `\n"
"          <li class='file-item'>\n"
"            <div>\n"
"              <div class='file-name'>☁ ${f.name}</div>\n"
"              <div class='file-meta'>Size: ${formatBytes(f.size)} | SHA: ${f.hash.substring(0, 8)}...</div>\n"
"            </div>\n"
"            <span class='tag-synced'>HOSTED IN REPO</span>\n"
"          </li>`;\n"
"      });\n"
"    } else {\n"
"      remoteList.innerHTML = `<li style='color: var(--text-muted); text-align: center; padding: 1.5rem;'>Remote repo is currently empty</li>`;\n"
"    }\n"
"  }\n"
"\n"
"  async function handleSmartButtonClick() {\n"
"    if (currentState === 'SYNCED' || currentState === 'SYNCING') return;\n"
"\n"
"    let action = (currentState === 'NEEDS_PULL') ? 'pull' : 'push';\n"
"    try {\n"
"      document.getElementById('smart-btn').className = 'btn-syncing';\n"
"      document.getElementById('btn-text').textContent = 'Executing...';\n"
"      await fetch('/api/action', {\n"
"        method: 'POST',\n"
"        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },\n"
"        body: `action=${action}`\n"
"      });\n"
"      setTimeout(fetchStatus, 300);\n"
"    } catch (e) {\n"
"      console.error(e);\n"
"    }\n"
"  }\n"
"\n"
"  setInterval(fetchStatus, 1000);\n"
"  fetchStatus();\n"
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

    write(fd, header, strlen(header));
    if (body_len > 0) {
        write(fd, body, body_len);
    }
}

typedef struct {
    repo_engine_t *engine;
    char action[16];
} action_arg_t;

static void *async_action_worker(void *arg) {
    action_arg_t *a = (action_arg_t *)arg;
    if (strcmp(a->action, "pull") == 0) {
        repo_engine_pull(a->engine);
    } else {
        repo_engine_push(a->engine);
    }
    free(a);
    return NULL;
}

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
                 "  \"push_count\": %zu,\n"
                 "  \"pull_count\": %zu,\n"
                 "  \"total_local\": %zu,\n"
                 "  \"total_remote\": %zu,\n"
                 "  \"progress\": %d,\n"
                 "  \"message\": \"%s\",\n"
                 "  \"push_files\": [",
                 repo_state_to_string(diff.state),
                 diff.pending_push_count,
                 diff.pending_pull_count,
                 diff.total_local_files,
                 diff.total_remote_files,
                 diff.progress_pct,
                 diff.status_message);

        for (size_t i = 0; i < diff.pending_push_count && i < 64; i++) {
            offset += snprintf(json_buf + offset, sizeof(json_buf) - (size_t)offset,
                               "%s\"%s\"", (i > 0 ? ", " : ""), diff.push_files[i]);
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
    } else if (strncmp(req_buf, "POST /api/action", 16) == 0) {
        char action[16] = "push";
        char *body = strstr(req_buf, "\r\n\r\n");
        if (body) {
            body += 4;
            if (strstr(body, "action=pull")) {
                strcpy(action, "pull");
            }
        }

        action_arg_t *arg = malloc(sizeof(action_arg_t));
        if (arg) {
            arg->engine = engine;
            strncpy(arg->action, action, sizeof(arg->action) - 1);
            pthread_t tid;
            pthread_create(&tid, NULL, async_action_worker, arg);
            pthread_detach(tid);
        }

        send_http_response(client_fd, "HTTP/1.1 200 OK", "application/json", "{\"status\":\"ok\"}");
    } else {
        send_http_response(client_fd, "HTTP/1.1 404 Not Found", "text/plain", "Not Found");
    }

    close(client_fd);
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

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        LOG_ERROR("HTTP", "socket creation failed: %s", strerror(errno));
        return -1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons((uint16_t)server->port);

    if (bind(listen_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        LOG_ERROR("HTTP", "bind failed on port %d: %s", server->port, strerror(errno));
        close(listen_fd);
        return -1;
    }

    if (listen(listen_fd, 32) < 0) {
        LOG_ERROR("HTTP", "listen failed: %s", strerror(errno));
        close(listen_fd);
        return -1;
    }

    server->is_running = 1;
    LOG_INFO("HTTP", "=========================================================");
    LOG_INFO("HTTP", " Smart Action Button UI live at: http://localhost:%d", server->port);
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

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd >= 0) {
            handle_http_client(client_fd, server->engine);
        }
    }

    close(listen_fd);
    LOG_INFO("HTTP", "HTTP server stopped");
    return 0;
}

void http_server_stop(http_server_t *server) {
    if (server) {
        server->is_running = 0;
    }
}
