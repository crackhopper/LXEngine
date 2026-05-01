(function () {
    "use strict";

    var endpoint = window.NOTES_CHAT_ENDPOINT || inferEndpoint();
    var storagePrefix = "notes-chat:";
    var state = {
        open: localStorage.getItem(storagePrefix + "open") === "1",
        busy: false,
        historyOpen: false,
        health: null,
        sessions: [],
        activeSessionId: localStorage.getItem(storagePrefix + "activeSessionId") || "",
        activeSession: null,
        currentDocPath: inferDocPath(),
        currentDocTitle: document.title || "",
        messages: [],
    };

    function inferEndpoint() {
        var port = Number(window.location.port || (window.location.protocol === "https:" ? 443 : 80));
        var host = window.location.hostname || "127.0.0.1";
        if (host === "0.0.0.0" || host === "::") {
            host = "127.0.0.1";
        }
        return window.location.protocol + "//" + host + ":" + String(port + 2);
    }

    function inferDocPath() {
        var path = window.location.pathname || "/";
        try {
            path = decodeURIComponent(path);
        } catch (error) {
            // Keep the raw path if decoding fails.
        }
        path = path.replace(/\\/g, "/");
        while (path.charAt(0) === "/") {
            path = path.slice(1);
        }
        if (path === "index.html") {
            return "README.md";
        }
        path = path.replace(/\/index\.html$/, "");
        path = path.replace(/\.html$/, ".md");
        while (path.endsWith("/")) {
            path = path.slice(0, -1);
        }
        if (!path) {
            return "README.md";
        }
        if (path.endsWith(".md")) {
            return path;
        }
        return path + ".md";
    }

    function selectedText() {
        var selection = window.getSelection ? window.getSelection() : null;
        var text = selection ? String(selection.toString() || "") : "";
        return text.trim().slice(0, 20000);
    }

    function api(path, options) {
        return fetch(endpoint + path, options || {}).then(function (response) {
            return response.json().then(function (payload) {
                if (!response.ok) {
                    throw new Error(payload.error || ("HTTP " + response.status));
                }
                return payload;
            });
        });
    }

    function createIcon(name) {
        var svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
        svg.setAttribute("viewBox", "0 0 24 24");
        svg.setAttribute("fill", "none");
        svg.setAttribute("stroke", "currentColor");
        svg.setAttribute("stroke-width", "2");
        svg.setAttribute("stroke-linecap", "round");
        svg.setAttribute("stroke-linejoin", "round");
        svg.setAttribute("aria-hidden", "true");

        function path(d) {
            var node = document.createElementNS("http://www.w3.org/2000/svg", "path");
            node.setAttribute("d", d);
            svg.appendChild(node);
        }

        if (name === "send") {
            path("m22 2-7 20-4-9-9-4Z");
            path("M22 2 11 13");
        } else if (name === "close") {
            path("M18 6 6 18");
            path("m6 6 12 12");
        } else if (name === "plus") {
            path("M12 5v14");
            path("M5 12h14");
        } else if (name === "history") {
            path("M3 12a9 9 0 1 0 3-6.7");
            path("M3 4v5h5");
            path("M12 7v5l3 2");
        } else if (name === "trash") {
            path("M3 6h18");
            path("M8 6V4h8v2");
            path("M6 6l1 15h10l1-15");
        } else {
            path("M21 15a4 4 0 0 1-4 4H8l-5 3V7a4 4 0 0 1 4-4h10a4 4 0 0 1 4 4z");
        }
        return svg;
    }

    function buildUi() {
        if (document.getElementById("notes-chat-root")) {
            return;
        }

        var root = document.createElement("div");
        root.id = "notes-chat-root";
        document.body.appendChild(root);
        var shadow = root.attachShadow({ mode: "open" });

        var style = document.createElement("style");
        style.textContent = [
            ":host{all:initial;color:#18202f;font-family:Inter,system-ui,-apple-system,BlinkMacSystemFont,\"Segoe UI\",sans-serif}",
            ".wrap{position:fixed;right:18px;bottom:18px;z-index:2147483600}",
            ".toggle{width:48px;height:48px;border:0;border-radius:50%;display:grid;place-items:center;background:#1d4ed8;color:white;box-shadow:0 10px 30px rgba(15,23,42,.25);cursor:pointer}",
            ".toggle:hover{background:#1e40af}",
            "svg{width:18px;height:18px}",
            ".toggle svg{width:23px;height:23px}",
            ".panel{position:absolute;right:0;bottom:60px;width:min(460px,calc(100vw - 28px));height:min(640px,calc(100vh - 92px));background:#fff;border:1px solid #d7dde8;border-radius:8px;box-shadow:0 18px 54px rgba(15,23,42,.24);display:none;overflow:hidden}",
            ".panel.open{display:grid;grid-template-rows:auto auto 1fr auto}",
            ".head{height:54px;display:grid;grid-template-columns:1fr auto;align-items:center;gap:10px;padding:0 10px 0 14px;border-bottom:1px solid #e4e8f0;background:#f8fafc}",
            ".title{min-width:0}",
            ".title strong{display:block;font-size:14px;line-height:18px;font-weight:650;color:#111827}",
            ".title span{display:block;font-size:12px;line-height:16px;color:#64748b;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:310px}",
            ".actions{display:flex;align-items:center;gap:4px}",
            ".icon{width:32px;height:32px;border:0;border-radius:6px;display:grid;place-items:center;background:transparent;color:#475569;cursor:pointer}",
            ".icon:hover{background:#e8edf5;color:#0f172a}",
            ".status{height:28px;padding:0 14px;display:flex;align-items:center;gap:8px;border-bottom:1px solid #edf1f7;background:#fff;font-size:12px;color:#64748b}",
            ".dot{width:7px;height:7px;border-radius:50%;background:#94a3b8;display:inline-block}",
            ".dot.ok{background:#16a34a}",
            ".layout{display:grid;grid-template-columns:1fr;min-height:0}",
            ".layout.history{grid-template-columns:168px 1fr}",
            ".historyPane{display:none;border-right:1px solid #e4e8f0;background:#f8fafc;overflow:auto}",
            ".layout.history .historyPane{display:block}",
            ".session{width:100%;border:0;background:transparent;text-align:left;padding:10px 9px;border-bottom:1px solid #e6ebf3;cursor:pointer;color:#111827}",
            ".session:hover,.session.active{background:#eaf0fb}",
            ".session strong{display:block;font-size:12px;line-height:16px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}",
            ".session span{display:block;font-size:11px;line-height:15px;color:#64748b;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}",
            ".msgs{padding:14px;overflow:auto;background:#ffffff;min-width:0;overscroll-behavior:contain}",
            ".msg{margin:0 0 12px;display:flex}",
            ".bubble{max-width:100%;border-radius:8px;padding:9px 10px;font-size:13px;line-height:1.5;white-space:pre-wrap;word-break:break-word}",
            ".user{justify-content:flex-end}",
            ".user .bubble{background:#1d4ed8;color:white}",
            ".agent .bubble{background:#f1f5f9;color:#172033;border:1px solid #e2e8f0}",
            ".error .bubble,.sys .bubble{background:#fff7ed;color:#8a3d0f;border:1px solid #fed7aa}",
            ".empty{font-size:13px;color:#64748b;padding:6px 0}",
            ".composer{display:grid;grid-template-columns:1fr 38px;gap:8px;padding:10px;border-top:1px solid #e4e8f0;background:#f8fafc}",
            "textarea{resize:none;height:42px;max-height:120px;border:1px solid #cbd5e1;border-radius:7px;padding:9px 10px;font:13px/1.45 inherit;color:#111827;background:#fff;outline:none}",
            "textarea:focus{border-color:#2563eb;box-shadow:0 0 0 3px rgba(37,99,235,.14)}",
            ".send{width:38px;height:42px;border:0;border-radius:7px;display:grid;place-items:center;background:#0f172a;color:#fff;cursor:pointer}",
            ".send[disabled]{background:#94a3b8;cursor:not-allowed}",
            ".hidden{display:none}"
        ].join("");
        shadow.appendChild(style);

        var wrap = document.createElement("div");
        wrap.className = "wrap";
        shadow.appendChild(wrap);

        var panel = document.createElement("section");
        panel.className = "panel";
        panel.setAttribute("aria-label", "文档 Chat");
        wrap.appendChild(panel);

        var head = document.createElement("header");
        head.className = "head";
        panel.appendChild(head);

        var title = document.createElement("div");
        title.className = "title";
        var titleText = document.createElement("strong");
        titleText.textContent = "文档 Chat";
        var subtitle = document.createElement("span");
        subtitle.textContent = state.currentDocPath;
        title.appendChild(titleText);
        title.appendChild(subtitle);
        head.appendChild(title);

        var actions = document.createElement("div");
        actions.className = "actions";
        head.appendChild(actions);

        var historyButton = iconButton("history", "历史会话");
        var newButton = iconButton("plus", "新建会话");
        var deleteButton = iconButton("trash", "删除当前会话");
        var close = iconButton("close", "关闭");
        actions.appendChild(historyButton);
        actions.appendChild(newButton);
        actions.appendChild(deleteButton);
        actions.appendChild(close);

        var status = document.createElement("div");
        status.className = "status";
        var dot = document.createElement("span");
        dot.className = "dot";
        var statusText = document.createElement("span");
        statusText.textContent = "连接中";
        status.appendChild(dot);
        status.appendChild(statusText);
        panel.appendChild(status);

        var layout = document.createElement("div");
        layout.className = "layout";
        panel.appendChild(layout);

        var historyPane = document.createElement("div");
        historyPane.className = "historyPane";
        layout.appendChild(historyPane);

        var messages = document.createElement("div");
        messages.className = "msgs";
        layout.appendChild(messages);
        var autoFollowMessages = true;

        var form = document.createElement("form");
        form.className = "composer";
        panel.appendChild(form);

        var input = document.createElement("textarea");
        input.placeholder = "问当前文档...";
        input.rows = 1;
        form.appendChild(input);

        var send = document.createElement("button");
        send.className = "send";
        send.type = "submit";
        send.title = "发送";
        send.appendChild(createIcon("send"));
        form.appendChild(send);

        var toggle = document.createElement("button");
        toggle.className = "toggle";
        toggle.type = "button";
        toggle.title = "打开文档 Chat";
        toggle.appendChild(createIcon("chat"));
        wrap.appendChild(toggle);

        function iconButton(icon, titleValue) {
            var button = document.createElement("button");
            button.className = "icon";
            button.type = "button";
            button.title = titleValue;
            button.appendChild(createIcon(icon));
            return button;
        }

        function setOpen(value) {
            state.open = value;
            localStorage.setItem(storagePrefix + "open", value ? "1" : "0");
            panel.classList.toggle("open", value);
            toggle.title = value ? "关闭文档 Chat" : "打开文档 Chat";
            if (value) {
                input.focus();
            }
        }

        function setHistoryOpen(value) {
            state.historyOpen = value;
            layout.classList.toggle("history", value);
            if (value) {
                renderSessions();
            }
        }

        function setBusy(value) {
            state.busy = value;
            input.disabled = value;
            send.disabled = value;
        }

        function isNearMessagesBottom() {
            return messages.scrollHeight - messages.scrollTop - messages.clientHeight <= 32;
        }

        function shouldFollowMessages() {
            return autoFollowMessages || isNearMessagesBottom();
        }

        function scrollMessagesToBottom() {
            messages.scrollTop = messages.scrollHeight;
            autoFollowMessages = true;
        }

        function restoreMessagesScroll(scrollTop) {
            var maxScroll = Math.max(0, messages.scrollHeight - messages.clientHeight);
            messages.scrollTop = Math.min(scrollTop, maxScroll);
            autoFollowMessages = isNearMessagesBottom();
        }

        function appendMessage(kind, text) {
            var row = document.createElement("div");
            row.className = "msg " + kind;
            var bubble = document.createElement("div");
            bubble.className = "bubble";
            bubble.textContent = text;
            row.appendChild(bubble);
            messages.appendChild(row);
            return bubble;
        }

        function addMessage(kind, text, options) {
            var forceScroll = options && options.forceScroll;
            var follow = forceScroll || shouldFollowMessages();
            var bubble = appendMessage(kind, text);
            if (follow) {
                scrollMessagesToBottom();
            }
            return bubble;
        }

        function renderMessages(options) {
            options = options || {};
            var previousScrollTop = messages.scrollTop;
            var follow = options.forceScroll || shouldFollowMessages();
            messages.textContent = "";
            if (!state.messages.length) {
                var empty = document.createElement("div");
                empty.className = "empty";
                empty.textContent = state.activeSessionId ? "这个会话还没有消息" : "发送第一条消息后会创建新会话";
                messages.appendChild(empty);
                if (follow) {
                    scrollMessagesToBottom();
                }
                return;
            }
            state.messages.forEach(function (message) {
                var role = message.role === "assistant" ? "agent" : message.role;
                appendMessage(role, message.text || "");
            });
            if (follow) {
                scrollMessagesToBottom();
            } else if (options.preserveScroll) {
                restoreMessagesScroll(previousScrollTop);
            }
        }

        function renderSessions() {
            historyPane.textContent = "";
            if (!state.sessions.length) {
                var empty = document.createElement("div");
                empty.className = "empty";
                empty.textContent = "暂无历史";
                historyPane.appendChild(empty);
                return;
            }
            state.sessions.forEach(function (session) {
                var button = document.createElement("button");
                button.className = "session" + (session.id === state.activeSessionId ? " active" : "");
                button.type = "button";
                var strong = document.createElement("strong");
                strong.textContent = session.title || "新会话";
                var meta = document.createElement("span");
                meta.textContent = (session.lastDocPath || "未关联文档") + " · " + shortTime(session.updatedAt);
                button.appendChild(strong);
                button.appendChild(meta);
                button.addEventListener("click", function () {
                    loadSession(session.id);
                });
                historyPane.appendChild(button);
            });
        }

        function shortTime(value) {
            if (!value) {
                return "";
            }
            return String(value).replace("T", " ").replace("Z", "");
        }

        function updateTitle() {
            if (state.activeSession) {
                titleText.textContent = state.activeSession.title || "文档 Chat";
                subtitle.textContent = state.currentDocPath;
            } else {
                titleText.textContent = "文档 Chat";
                subtitle.textContent = state.currentDocPath;
            }
        }

        function resolveCurrentDoc() {
            return api("/doc?path=" + encodeURIComponent(state.currentDocPath), { cache: "no-store" }).then(function (payload) {
                state.currentDocPath = payload.path || state.currentDocPath;
                state.currentDocTitle = payload.title || state.currentDocTitle;
                updateTitle();
                return payload;
            });
        }

        function loadSessions() {
            return api("/sessions", { cache: "no-store" }).then(function (payload) {
                state.sessions = payload.sessions || [];
                renderSessions();
            });
        }

        function startDraftSession() {
            state.activeSessionId = "";
            localStorage.removeItem(storagePrefix + "activeSessionId");
            state.activeSession = null;
            state.messages = [];
            renderMessages({ forceScroll: true });
            updateTitle();
            renderSessions();
        }

        function createSession() {
            return api("/sessions", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({ docPath: state.currentDocPath, title: "新会话" })
            }).then(function (payload) {
                var session = payload.session;
                state.activeSessionId = session.id;
                localStorage.setItem(storagePrefix + "activeSessionId", session.id);
                state.activeSession = session;
                state.messages = [];
                renderMessages({ forceScroll: true });
                updateTitle();
                return loadSessions();
            });
        }

        function ensureSession() {
            if (state.activeSessionId) {
                return loadSession(state.activeSessionId).catch(function () {
                    return createSession();
                });
            }
            return createSession();
        }

        function loadSession(sessionId, options) {
            return api("/sessions/" + encodeURIComponent(sessionId), { cache: "no-store" }).then(function (payload) {
                var session = payload.session;
                state.activeSessionId = session.id;
                localStorage.setItem(storagePrefix + "activeSessionId", session.id);
                state.activeSession = session;
                state.messages = session.messages || [];
                renderMessages(options || { forceScroll: true });
                updateTitle();
                renderSessions();
            });
        }

        function deleteCurrentSession() {
            if (!state.activeSessionId || state.busy) {
                return;
            }
            api("/sessions/" + encodeURIComponent(state.activeSessionId), { method: "DELETE" }).then(function () {
                state.activeSessionId = "";
                localStorage.removeItem(storagePrefix + "activeSessionId");
                state.activeSession = null;
                state.messages = [];
                renderMessages({ forceScroll: true });
                updateTitle();
                return loadSessions();
            }).then(function () {
                if (state.sessions.length) {
                    return loadSession(state.sessions[0].id);
                }
                startDraftSession();
                return null;
            }).catch(function (error) {
                addMessage("sys", error.message || String(error));
            });
        }

        function parseSseBlock(block) {
            var event = "message";
            var data = [];
            block.split(/\r?\n/).forEach(function (line) {
                if (line.indexOf("event:") === 0) {
                    event = line.slice(6).trim();
                } else if (line.indexOf("data:") === 0) {
                    data.push(line.slice(5).trimStart());
                }
            });
            var payload = {};
            if (data.length) {
                try {
                    payload = JSON.parse(data.join("\n"));
                } catch (error) {
                    payload = { text: data.join("\n") };
                }
            }
            return { event: event, payload: payload };
        }

        function handleSseEvent(item, pendingBubble) {
            if (item.event === "session" && item.payload.session) {
                state.activeSession = item.payload.session;
                state.activeSessionId = item.payload.session.id;
                localStorage.setItem(storagePrefix + "activeSessionId", state.activeSessionId);
                updateTitle();
                return;
            }
            if (item.event === "delta") {
                var follow = shouldFollowMessages();
                pendingBubble.textContent += item.payload.text || "";
                if (follow) {
                    scrollMessagesToBottom();
                }
                return;
            }
            if (item.event === "done") {
                if (item.payload.session) {
                    state.activeSession = item.payload.session;
                    state.activeSessionId = item.payload.session.id;
                    localStorage.setItem(storagePrefix + "activeSessionId", state.activeSessionId);
                }
                updateTitle();
                loadSessions();
                return;
            }
            if (item.event === "error") {
                throw new Error(item.payload.error || "stream error");
            }
        }

        function streamChat(message, pendingBubble) {
            return fetch(endpoint + "/chat/stream", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify({
                    sessionId: state.activeSessionId,
                    docPath: state.currentDocPath,
                    pagePath: window.location.pathname,
                    docTitle: state.currentDocTitle || document.title,
                    selectedText: selectedText(),
                    message: message
                })
            }).then(function (response) {
                if (!response.ok || !response.body) {
                    return response.text().then(function (text) {
                        throw new Error(text || ("HTTP " + response.status));
                    });
                }
                var reader = response.body.getReader();
                var decoder = new TextDecoder();
                var buffer = "";

                function pump() {
                    return reader.read().then(function (result) {
                        if (result.done) {
                            if (buffer.trim()) {
                                handleSseEvent(parseSseBlock(buffer), pendingBubble);
                            }
                            return;
                        }
                        buffer += decoder.decode(result.value, { stream: true });
                        var parts = buffer.split(/\r?\n\r?\n/);
                        buffer = parts.pop() || "";
                        parts.forEach(function (part) {
                            if (part.trim()) {
                                handleSseEvent(parseSseBlock(part), pendingBubble);
                            }
                        });
                        return pump();
                    });
                }

                return pump();
            });
        }

        toggle.addEventListener("click", function () {
            setOpen(!state.open);
        });
        close.addEventListener("click", function () {
            setOpen(false);
        });
        historyButton.addEventListener("click", function () {
            setHistoryOpen(!state.historyOpen);
        });
        newButton.addEventListener("click", function () {
            if (!state.busy) {
                startDraftSession();
                input.focus();
            }
        });
        deleteButton.addEventListener("click", deleteCurrentSession);
        messages.addEventListener("scroll", function () {
            autoFollowMessages = isNearMessagesBottom();
        }, { passive: true });

        input.addEventListener("keydown", function (event) {
            if (event.key === "Enter" && (event.metaKey || event.ctrlKey)) {
                form.requestSubmit();
            }
        });

        form.addEventListener("submit", function (event) {
            event.preventDefault();
            var message = input.value.trim();
            if (!message || state.busy) {
                return;
            }
            setBusy(true);
            input.value = "";
            ensureSession().then(function () {
                if (messages.querySelector(".empty")) {
                    messages.textContent = "";
                }
                autoFollowMessages = true;
                addMessage("user", message, { forceScroll: true });
                var pending = addMessage("agent", "", { forceScroll: true });
                return streamChat(message, pending);
            }).then(function () {
                return loadSession(state.activeSessionId, { preserveScroll: true });
            }).catch(function (error) {
                addMessage("sys", error.message || String(error));
            }).finally(function () {
                setBusy(false);
                input.focus();
            });
        });

        setOpen(state.open);
        renderMessages({ forceScroll: true });

        fetch(endpoint + "/health", { cache: "no-store" }).then(function (response) {
            if (!response.ok) {
                throw new Error("Chat service unavailable");
            }
            return response.json();
        }).then(function (payload) {
            state.health = payload;
            dot.classList.add("ok");
            statusText.textContent = "已连接 · " + ((payload.agent && payload.agent.name) || "agent");
            return resolveCurrentDoc();
        }).then(function () {
            return loadSessions();
        }).then(function () {
            if (state.activeSessionId) {
                return loadSession(state.activeSessionId).catch(function () {
                    startDraftSession();
                    return null;
                });
            }
            if (state.sessions.length) {
                return loadSession(state.sessions[0].id);
            }
            startDraftSession();
            return null;
        }).catch(function (error) {
            statusText.textContent = "Chat 初始化失败：" + (error.message || endpoint);
            addMessage("sys", "Chat 初始化失败：" + (error.message || endpoint));
        });
    }

    if (document.readyState === "loading") {
        document.addEventListener("DOMContentLoaded", buildUi, { once: true });
    } else {
        buildUi();
    }
}());
