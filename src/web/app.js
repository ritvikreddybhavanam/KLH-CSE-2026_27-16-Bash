const page = document.querySelector('#page');
const navItems = [...document.querySelectorAll('.nav-item')];
const labels = Object.fromEntries(navItems.map((item) => [item.dataset.view, item.textContent.replace(/^\d+\s*/, '').trim()]));
const state = { data: null, view: 'dashboard', selectedPid: null, history: [], snapshots: loadSnapshots(), search: '', sort: 'cpu_percent' };

function loadSnapshots() {
  try { return JSON.parse(localStorage.getItem('linux-monitor-snapshots') || '[]'); }
  catch { return []; }
}

function escapeHtml(value) {
  return String(value ?? '').replace(/[&<>"']/g, (character) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[character]);
}

function number(value, digits = 1) {
  return Number.isFinite(Number(value)) ? Number(value).toFixed(digits) : '0.0';
}

function formatBytes(mb) {
  const value = Number(mb) || 0;
  return value >= 1024 ? `${number(value / 1024, 2)} GB` : `${number(value, 0)} MB`;
}

function formatUptime(seconds) {
  const value = Math.max(0, Number(seconds) || 0);
  const days = Math.floor(value / 86400);
  const hours = Math.floor((value % 86400) / 3600);
  const minutes = Math.floor((value % 3600) / 60);
  return days ? `${days}d ${hours}h ${minutes}m` : `${hours}h ${minutes}m`;
}

function system() { return state.data?.system; }

function setConnection(status) {
  const connected = status === 'live';
  document.querySelector('#connection-dot').classList.toggle('connected', connected);
  document.querySelector('#connection-label').textContent = connected ? 'Live Linux collector connected' : status === 'stale' ? 'Collector snapshot is stale' : 'Waiting for Linux collector';
  document.querySelector('#data-mode').textContent = connected ? 'LIVE UBUNTU DATA' : status === 'stale' ? 'STALE SNAPSHOT' : 'NO LIVE DATA';
  document.querySelector('#data-mode').classList.toggle('live', connected);
  document.querySelector('#updated-at').textContent = state.data ? `Updated ${new Date(state.data.timestamp).toLocaleTimeString()}` : 'No collector snapshot received';
}

function heading(title, subtitle, eyebrow = 'SYSTEM OBSERVABILITY') {
  return `<header class="page-heading"><div><p class="eyebrow">${escapeHtml(eyebrow)}</p><h1>${escapeHtml(title)}</h1><p class="heading-copy">${escapeHtml(subtitle)}</p></div>${state.data ? `<span class="updated-chip">SNAPSHOT ${escapeHtml(new Date(state.data.timestamp).toLocaleTimeString())}</span>` : ''}</header>`;
}

function liveRequired() {
  return '<div class="empty-state"><div class="empty-mark">/proc → C COLLECTOR → JSON</div><h2>Waiting for live Ubuntu data</h2><p>This dashboard does not invent fallback metrics. In WSL/Ubuntu, run <code>./monitor --web-export web/data.json</code>, then refresh this page.</p></div>';
}

function metric(label, value, sub) {
  return `<div class="metric"><div class="metric-label">${escapeHtml(label)}</div><div class="metric-value">${escapeHtml(value)}</div><div class="metric-sub">${escapeHtml(sub)}</div></div>`;
}

function panel(title, body, kicker = '') {
  return `<section class="panel"><header class="panel-head"><h2 class="panel-title">${escapeHtml(title)}</h2>${kicker ? `<span class="panel-kicker">${escapeHtml(kicker)}</span>` : ''}</header><div class="panel-body">${body}</div></section>`;
}

function sourceLine(source, command) {
  return `<div class="source-line"><span>SOURCE</span><span class="source-tag">${escapeHtml(source)}</span><span>COMMAND</span><span>${escapeHtml(command)}</span></div>`;
}

function commandBlock(item) {
  return `<section class="command-block"><div class="command-title"><code>$ ${escapeHtml(item.command)}</code><span>UBUNTU CAPTURE</span></div><pre>${escapeHtml(item.output || 'No command output captured.')}</pre></section>`;
}

function processTable(processes, limit = Infinity) {
  const rows = processes.slice(0, limit).map((process) => `<tr data-pid="${Number(process.pid)}"><td class="pid-link">${Number(process.pid)}</td><td>${Number(process.ppid)}</td><td>${escapeHtml(process.name)}</td><td><span class="state-pill">${escapeHtml(process.state)}</span></td><td>${number(process.cpu_percent)}%</td><td>${number(process.memory_percent)}%</td><td>${formatBytes(process.memory_mb)}</td></tr>`).join('');
  return `<div class="table-wrap"><table><thead><tr><th>PID</th><th>PPID</th><th>Name</th><th>State</th><th>CPU %</th><th>MEM %</th><th>Resident</th></tr></thead><tbody>${rows || '<tr><td colspan="7">No matching processes in this snapshot.</td></tr>'}</tbody></table></div>`;
}

function stateDistribution() {
  const meanings = { R: 'Running / runnable', S: 'Interruptible sleep', D: 'Uninterruptible sleep', T: 'Stopped', t: 'Tracing stop', Z: 'Zombie', X: 'Dead', I: 'Idle kernel thread' };
  const counts = state.data.process_states || {};
  const total = Math.max(1, state.data.processes.length);
  return `<div class="state-list">${Object.entries(meanings).map(([code, meaning]) => {
    const count = Number(counts[code] || 0);
    return `<div class="state-row"><span class="state-pill">${code}</span><span>${meaning}<div class="bar-track"><div class="bar-fill" style="width:${Math.min(100, count * 100 / total)}%"></div></div></span><span class="state-count">${count}</span></div>`;
  }).join('')}</div>`;
}

function chartCanvas(id, label) {
  return `<div class="chart-wrap"><canvas id="${id}" aria-label="${escapeHtml(label)}"></canvas></div>`;
}

function findCommand(name) {
  return state.data?.commands?.find((item) => item.name === name) || { command: 'waiting for collector', output: 'No live command output captured.' };
}

function renderDashboard() {
  if (!state.data) return heading('Live system dashboard', 'Real Ubuntu metrics collected by the existing C monitor.') + liveRequired();
  const sys = system();
  const recent = [...state.data.processes].sort((a, b) => b.cpu_percent - a.cpu_percent);
  return `${heading('Live system dashboard', 'Real-time Linux metrics and process activity from this Ubuntu host.')}<div class="metric-grid">${metric('CPU usage', `${number(sys.cpu_usage_pct)}%`, `USER ${number(sys.cpu_user_pct)}% · SYSTEM ${number(sys.cpu_system_pct)}%`)}${metric('Memory used', formatBytes(sys.memory_used_mb), `${formatBytes(sys.memory_total_mb)} total · ${number(sys.memory_usage_pct)}%`)}${metric('Processes', sys.process_count, `${state.data.process_states.R || 0} running now`)}${metric('Load average', `${number(sys.load_1m, 2)}`, `5m ${number(sys.load_5m, 2)} · 15m ${number(sys.load_15m, 2)}`)}${metric('Uptime', formatUptime(sys.uptime_seconds), 'since system boot')}</div><div class="columns"><div class="panel"><header class="panel-head"><h2 class="panel-title">CPU utilization</h2><span class="panel-kicker">REAL /proc/stat SAMPLES</span></header>${chartCanvas('cpu-chart', 'CPU usage history')}${sourceLine(state.data.sources.cpu, 'cat /proc/stat')}</div>${panel('Process states', stateDistribution(), 'LIVE COUNTS FROM /proc')}</div><div class="columns">${panel('Processes by CPU', processTable(recent, 8), 'SELECT A ROW FOR DETAILS')}${panel('Memory overview', `<div class="detail-grid"><div class="detail-cell"><div class="detail-key">Available</div><div class="detail-value">${formatBytes(sys.memory_available_mb)}</div></div><div class="detail-cell"><div class="detail-key">Free</div><div class="detail-value">${formatBytes(sys.memory_free_mb)}</div></div><div class="detail-cell"><div class="detail-key">Cached</div><div class="detail-value">${formatBytes(sys.memory_cached_mb)}</div></div><div class="detail-cell"><div class="detail-key">Swap used</div><div class="detail-value">${formatBytes(sys.swap_used_mb)}</div></div></div>${sourceLine(state.data.sources.memory, 'free -h')}`, 'MEMORY SNAPSHOT')}</div><div class="columns">${panel('Ubuntu uptime output', commandBlock(findCommand('Uptime and load')), 'CAPTURED FROM THIS HOST')}${panel('Current memory command', commandBlock(findCommand('Memory')), 'CAPTURED FROM THIS HOST')}</div>`;
}

function renderProcesses() {
  if (!state.data) return heading('Live process monitor', 'Every row comes from the C process scanner reading /proc.') + liveRequired();
  const processes = [...state.data.processes].filter((process) => `${process.pid} ${process.ppid} ${process.name}`.toLowerCase().includes(state.search.toLowerCase()));
  processes.sort((a, b) => Number(b[state.sort] || 0) - Number(a[state.sort] || 0));
  return `${heading('Live process monitor', `${state.data.processes.length} discovered processes. Filter by PID or name; select a row to inspect actual process details.`)}<section class="panel"><header class="panel-head"><h2 class="panel-title">/proc process snapshot</h2><div class="toolbar"><input class="input" id="process-search" placeholder="Filter PID or process name" value="${escapeHtml(state.search)}"><select class="select" id="process-sort"><option value="cpu_percent" ${state.sort === 'cpu_percent' ? 'selected' : ''}>Sort: CPU</option><option value="memory_percent" ${state.sort === 'memory_percent' ? 'selected' : ''}>Sort: Memory</option><option value="pid" ${state.sort === 'pid' ? 'selected' : ''}>Sort: PID</option></select></div></header>${processTable(processes)}<div class="panel-body">${sourceLine(state.data.sources.processes, 'ps -eo pid,ppid,stat,comm,%cpu,%mem')}</div></section>`;
}

function renderProcessDetail() {
  if (!state.data) return heading('Process details', 'Inspect live metadata for a process selected from the process monitor.') + liveRequired();
  const proc = state.data.processes.find((item) => item.pid === state.selectedPid);
  if (!proc) return `${heading('Process details', 'Select a process in Live processes to see its actual /proc metadata.')}<div class="empty-state"><div class="empty-mark">PID → /proc/[PID]</div><h2>No process selected</h2><p>Open Live processes and select a row. Processes may disappear between snapshots; that is normal on a live Linux system.</p></div>`;
  const parent = state.data.processes.find((item) => item.pid === proc.ppid);
  const stateNames = { R: 'Running / runnable', S: 'Interruptible sleep', D: 'Uninterruptible sleep', T: 'Stopped', Z: 'Zombie', I: 'Idle' };
  const cells = [['PID', proc.pid], ['Parent PID', proc.ppid], ['Process name', proc.name], ['State', `${proc.state} · ${stateNames[proc.state] || 'Other'}`], ['CPU', `${number(proc.cpu_percent)}%`], ['Memory', `${formatBytes(proc.memory_mb)} · ${number(proc.memory_percent)}%`], ['Parent process', parent ? `${parent.name} (${parent.pid})` : 'Not present in current process snapshot'], ['Threads', proc.threads], ['Started', proc.start_time_epoch ? new Date(proc.start_time_epoch * 1000).toLocaleString() : 'Unavailable']];
  const sources = proc.sources || {};
  return `${heading(`Process ${proc.pid}`, 'Fields are read by the C monitor from the selected process and its /proc entries.', 'LIVE PROCESS DETAILS')}<div class="detail-grid">${cells.map(([key, value]) => `<div class="detail-cell"><div class="detail-key">${escapeHtml(key)}</div><div class="detail-value">${escapeHtml(value)}</div></div>`).join('')}</div><div class="columns">${panel('Command line', `<div class="command-block"><pre>${escapeHtml(proc.command_line || proc.name)}</pre></div>${sourceLine(sources.command_line || `/proc/${proc.pid}/cmdline`, `ps -p ${proc.pid} -o pid,ppid,stat,cmd`)}`)}${panel('Exact /proc sources', `<div class="state-list">${Object.entries(sources).map(([field, path]) => `<div class="snapshot-row"><span>${escapeHtml(field.replaceAll('_', ' '))}</span><span class="source-tag">${escapeHtml(path)}</span></div>`).join('')}</div><p class="heading-copy">CPU and state come from <code>stat</code>; resident memory comes from <code>statm</code>; thread count comes from <code>status</code>; command line comes from <code>cmdline</code>.</p>${sourceLine(`/proc/${proc.pid}/stat`, `cat /proc/${proc.pid}/stat`)}`)}</div>`;
}

function renderTree() {
  if (!state.data) return heading('Process tree', 'A dynamic PID → PPID hierarchy from the current Linux process snapshot.') + liveRequired();
  const byPid = new Map(state.data.processes.map((process) => [process.pid, process]));
  const children = new Map();
  state.data.processes.forEach((proc) => {
    if (!children.has(proc.ppid)) children.set(proc.ppid, []);
    children.get(proc.ppid).push(proc);
  });
  children.forEach((items) => items.sort((a, b) => a.pid - b.pid));
  const visited = new Set();
  const lines = [];
  function visit(proc, prefix, last, depth) {
    if (visited.has(proc.pid) || depth > 32) return;
    visited.add(proc.pid);
    lines.push(`${prefix}${depth ? (last ? '└─ ' : '├─ ') : ''}${proc.name} (${proc.pid})`);
    const nextPrefix = depth ? `${prefix}${last ? '   ' : '│  '}` : '';
    const descendants = (children.get(proc.pid) || []).filter((child) => !visited.has(child.pid));
    descendants.forEach((child, index) => visit(child, nextPrefix, index === descendants.length - 1, depth + 1));
  }
  const roots = state.data.processes.filter((proc) => proc.ppid === 0 || !byPid.has(proc.ppid)).sort((a, b) => a.pid - b.pid);
  roots.forEach((proc) => visit(proc, '', true, 0));
  state.data.processes.forEach((proc) => { if (!visited.has(proc.pid)) visit(proc, '', true, 0); });
  const pstree = findCommand('Process tree');
  const treeSource = pstree.output.trim() ? commandBlock(pstree) : '<div class="notice"><strong>pstree output unavailable.</strong> The hierarchy above is built dynamically from exported PID and PPID data.</div>';
  return `${heading('Process hierarchy', 'The tree is assembled in the browser from actual PIDs and parent PIDs in this snapshot.')}<div class="columns"><section class="panel"><header class="panel-head"><h2 class="panel-title">PID → PPID hierarchy</h2><span class="panel-kicker">${state.data.processes.length} PROCESSES</span></header><pre class="tree-view">${escapeHtml(lines.join('\n'))}</pre>${sourceLine(state.data.sources.processes, 'pstree -p (when installed)')}</section><div>${treeSource}</div></div>`;
}

function renderCpuMemory() {
  if (!state.data) return heading('CPU & memory', 'Sampled resource values are calculated from Linux kernel interfaces.') + liveRequired();
  const sys = system();
  const memoryRows = [['Total', sys.memory_total_mb], ['Used (total − available)', sys.memory_used_mb], ['Available', sys.memory_available_mb], ['Free', sys.memory_free_mb], ['Cached + reclaimable', sys.memory_cached_mb], ['Swap used', sys.swap_used_mb], ['Swap free', sys.swap_free_mb]];
  const cpuRows = [['User', sys.cpu_user_pct], ['System', sys.cpu_system_pct], ['Idle', sys.cpu_idle_pct], ['I/O wait', sys.cpu_iowait_pct]];
  return `${heading('CPU & memory', 'CPU is sampled across successive /proc/stat reads; memory comes from /proc/meminfo.')}<div class="columns"><div class="panel"><header class="panel-head"><h2 class="panel-title">CPU utilization</h2><span class="panel-kicker">2 SECOND SAMPLE INTERVAL</span></header>${chartCanvas('cpu-chart', 'CPU usage history')}<div class="panel-body"><div class="detail-grid">${cpuRows.map(([name, value]) => `<div class="detail-cell"><div class="detail-key">${name}</div><div class="detail-value">${number(value)}%</div></div>`).join('')}</div>${sourceLine(state.data.sources.cpu, 'cat /proc/stat')}</div></div><div class="panel"><header class="panel-head"><h2 class="panel-title">Memory utilization history</h2><span class="panel-kicker">REAL SAMPLES</span></header>${chartCanvas('memory-chart', 'Memory usage history')}<div class="panel-body">${sourceLine(state.data.sources.memory, 'free -h')}</div></div></div><div class="columns"><div class="panel"><header class="panel-head"><h2 class="panel-title">Memory and swap</h2><span class="panel-kicker">LIVE /proc/meminfo</span></header><div class="panel-body"><div class="state-list">${memoryRows.map(([name, value]) => `<div class="state-row"><span></span><span>${name}<div class="bar-track"><div class="bar-fill" style="width:${Math.min(100, Number(value) * 100 / Math.max(1, sys.memory_total_mb))}%"></div></div></span><span class="state-count">${formatBytes(value)}</span></div>`).join('')}</div>${sourceLine(state.data.sources.memory, 'free -h')}</div></div>${panel('Ubuntu memory command', commandBlock(findCommand('Memory')))}</div><div class="columns">${panel('Kernel memory counters', commandBlock(findCommand('Memory counters')))}${panel('CPU counters', commandBlock(findCommand('CPU counters')))}</div>`;
}

function renderCommands() {
  if (!state.data) return heading('Ubuntu commands', 'Command output is captured by the Linux collector on the same host as the dashboard.') + liveRequired();
  const provenance = [...(state.data.source_map || []), ...(state.data.process_list_map || [])];
  const mapRows = provenance.map((item) => `<tr><td>${escapeHtml(item.metric)}</td><td>${escapeHtml(item.source)}</td><td>${escapeHtml(item.calculation)}</td></tr>`).join('');
  return `${heading('Ubuntu commands & data sources', 'The C collector reads /proc directly. Commands below are captured comparison evidence from the same Ubuntu host, not inputs used by the collector.')}<div class="notice"><strong>Data path:</strong> Ubuntu kernel interfaces → existing C functions in <code>system.c</code> and <code>process.c</code> → atomic <code>web/data.json</code> → this dashboard. No process or system metrics are hardcoded.</div><section class="panel" style="margin-top:12px"><header class="panel-head"><h2 class="panel-title">Website metric → exact C input</h2><span class="panel-kicker">DIRECT /proc READS</span></header><div class="table-wrap"><table><thead><tr><th>Website data</th><th>C reads</th><th>How C derives the value</th></tr></thead><tbody>${mapRows}</tbody></table></div></section><div class="split-cards" style="margin-top:12px">${state.data.commands.map((item) => `<div>${commandBlock(item)}</div>`).join('')}</div>`;
}

const demoSections = {
  ipc: { title: 'IPC demonstrations', subtitle: 'Run the existing demonstrations in the terminal to see their actual messages and child-process behavior.', eyebrow: 'OS LAB · CO-3', items: [['Anonymous pipe', 'pipe(), fork(), read(), write()', 'Parent writes a message to a pipe; the child reads it.', '9'], ['FIFO', 'mkfifo(), open(), read(), write()', 'A named pipe carries data between processes.', '9'], ['Signals', 'sigaction(), kill(), pause()', 'A process sends a signal and the receiver handles it.', '10'], ['Unix domain socket', 'socketpair(), send(), recv()', 'Local processes exchange a message over a socket.', '16'], ['POSIX shared memory', 'shm_open(), mmap()', 'Processes map a shared object and read/write the same region.', '17']] },
  memory: { title: 'Memory demonstrations', subtitle: 'Software demonstrations explain virtual memory; kernel-observed process resident memory comes from /proc/[PID]/statm.', eyebrow: 'OS LAB · CO-4', items: [['Allocation cycle', 'malloc(), realloc(), free()', 'Shows user-space allocation behavior, not physical page tables.', '11'], ['Copy-on-Write', 'fork(), memory writes', 'The child initially shares pages; writes trigger kernel-managed copies.', '11'], ['Page faults', 'getrusage()', 'Reports process fault counters through the OS resource-usage interface.', '11'], ['Process memory', '/proc/[PID]/statm', 'The live process table reports resident memory read by the C scanner.', 'Live data']] },
  files: { title: 'File I/O', subtitle: 'Connect the existing file demonstrations to Linux file metadata and mount information.', eyebrow: 'OS LAB · CO-5', items: [['File descriptors', 'open(), read(), write(), close()', 'Demonstrates descriptor-based file operations.', '12'], ['Seek and records', 'lseek(), stat()', 'Shows seeking and structured file data.', '12'], ['Mount information', '/proc/self/mountinfo, statvfs()', 'Reads the current process mount namespace and filesystem details.', '12'], ['System call path', 'open() → write() → close()', 'Connects a user request to a Linux kernel service.', '14']] },
  threads: { title: 'Threads & synchronization', subtitle: 'Use the current C demos to observe pthread execution and synchronization results in the terminal.', eyebrow: 'OS LAB · CO-6', items: [['Mutex', 'pthread_mutex_lock(), pthread_mutex_unlock()', 'Protects a shared resource from concurrent updates.', '13'], ['Condition variable', 'pthread_cond_wait(), pthread_cond_signal()', 'Coordinates threads that wait for a state change.', '13'], ['Semaphore', 'sem_wait(), sem_post()', 'Controls access using a shared counter.', '13'], ['Race and fairness', 'pthread_create(), scheduling', 'The race and starvation demos print their actual runtime results.', '13 and 18']] }
};

function renderDemos(key) {
  const info = demoSections[key];
  const cards = info.items.map(([title, api, description, choice]) => `<article class="info-card"><h3>${escapeHtml(title)}</h3><p>${escapeHtml(description)}</p><div class="source-line"><span>C API</span><span class="source-tag">${escapeHtml(api)}</span></div><div class="source-line"><span>RUN</span><span>./monitor → menu ${escapeHtml(choice)}</span></div></article>`).join('');
  return `${heading(info.title, info.subtitle, info.eyebrow)}<div class="notice"><strong>No demo output is fabricated or labeled live.</strong> Run the stated menu option in Ubuntu to display its actual output in the terminal. System metrics remain sourced from live /proc data.</div><div class="split-cards" style="margin-top:12px">${cards}</div>`;
}

function renderConcepts() {
  const concepts = [['Processes', 'A process is a running program with an ID, state, memory mappings, and a parent relationship.', '/proc/[PID]/stat · process.c'], ['System calls', 'Programs request kernel services through system-call interfaces; file APIs are a common example.', 'system.c · file_demo.c'], ['IPC', 'Processes communicate using pipes, FIFOs, signals, sockets, and shared memory.', 'ipc.c'], ['Virtual memory', 'Each process gets a virtual address space; the kernel manages mappings and page faults.', 'memory_demo.c · /proc/[PID]/statm'], ['File systems', 'File descriptors, inodes, mount namespaces, and metadata connect programs to storage.', 'file_demo.c · /proc/self/mountinfo'], ['Concurrency', 'Threads share process memory and coordinate access with synchronization primitives.', 'thread_monitor.c']];
  return `${heading('OS concepts', 'Connect live system measurements to operating-system abstractions implemented in this project.', 'OS COURSE OUTCOMES')}<div class="split-cards">${concepts.map(([title, text, source]) => `<article class="info-card"><h3>${escapeHtml(title)}</h3><p>${escapeHtml(text)}</p><div class="source-line"><span>IMPLEMENTATION</span><span class="source-tag">${escapeHtml(source)}</span></div></article>`).join('')}</div>`;
}

function renderCoMapping() {
  const rows = [['CO-1', 'System calls', 'system.c · file_demo.c', 'Kernel metrics, file operations'], ['CO-2', 'Processes', 'process.c · process_lifecycle_demo.c', 'Live PID/PPID, state, hierarchy'], ['CO-3', 'IPC', 'ipc.c', 'Pipe, FIFO, signal, socket, shared memory'], ['CO-4', 'Memory management', 'memory_demo.c · system.c', '/proc memory and page-fault demonstrations'], ['CO-5', 'File systems & I/O', 'file_demo.c', 'File descriptors and mount information'], ['CO-6', 'Concurrency', 'thread_monitor.c', 'Thread and synchronization demonstrations']];
  return `${heading('Course outcome mapping', 'Implementation references and live Ubuntu evidence used in the project.', 'ACADEMIC TRACEABILITY')}<section class="panel"><div class="table-wrap"><table><thead><tr><th>Outcome</th><th>Concept</th><th>Implementation</th><th>Ubuntu evidence / visualization</th></tr></thead><tbody>${rows.map((row) => `<tr>${row.map((cell) => `<td>${escapeHtml(cell)}</td>`).join('')}</tr>`).join('')}</tbody></table></div></section>`;
}

function renderArchitecture() {
  const stages = [['01 · UBUNTU', 'Linux kernel exports system and process state'], ['02 · /proc', '/proc/stat, meminfo, uptime, loadavg, PID entries'], ['03 · C MONITOR', 'Existing system.c, process.c and MonitorState'], ['04 · JSON', 'Atomic web/data.json refreshed every two seconds'], ['05 · DASHBOARD', 'Browser reads local JSON; it cannot read /proc']];
  const runSteps = ['make', './monitor --web-export web/data.json', '', '# In another terminal, from the project root:', 'python3 -m http.server 8000 --directory web', '', 'Open http://localhost:8000'];
  return `${heading('Project architecture', 'A local data path from Ubuntu kernel interfaces through the existing C implementation into the browser.', 'DATA FLOW')}<div class="steps">${stages.map(([title, detail]) => `<div class="step"><strong>${escapeHtml(title)}</strong><span>${escapeHtml(detail)}</span></div>`).join('')}</div><div class="columns">${panel('Data provenance', `<div class="detail-grid"><div class="detail-cell"><div class="detail-key">CPU</div><div class="detail-value">/proc/stat</div></div><div class="detail-cell"><div class="detail-key">Memory</div><div class="detail-value">/proc/meminfo</div></div><div class="detail-cell"><div class="detail-key">Processes</div><div class="detail-value">/proc/[PID]/*</div></div></div><p class="heading-copy">The browser fetches a local JSON file over HTTP. The collector uses existing C APIs and never labels browser-generated or sample metrics as live.</p>`)}${panel('Start the local demo', `<div class="command-block"><div class="command-title"><code>Ubuntu / WSL</code></div><pre>${escapeHtml(runSteps.join('\n'))}</pre></div>`)}</div>`;
}

function renderSnapshots() {
  const rows = state.snapshots.map((snapshot, index) => `<div class="snapshot-row"><span>${escapeHtml(new Date(snapshot.timestamp).toLocaleString())} · CPU ${number(snapshot.system.cpu_usage_pct)}% · ${snapshot.system.process_count} processes</span><button class="button-small" data-remove-snapshot="${index}" title="Remove snapshot">Remove</button></div>`).join('');
  return `${heading('Captured snapshots', 'Snapshots are saved in this browser and contain the actual Linux JSON payload.', 'LOCAL EVIDENCE')}<section class="panel"><header class="panel-head"><h2 class="panel-title">Snapshot history</h2><span class="panel-kicker">LOCAL BROWSER STORAGE</span></header><div class="panel-body">${rows || '<p class="heading-copy">No snapshots captured yet. Use Capture snapshot while live data is connected.</p>'}</div></section>`;
}

function render() {
  const focusedId = document.activeElement?.id;
  const selection = focusedId === 'process-search' ? document.activeElement.selectionStart : null;
  navItems.forEach((item) => item.classList.toggle('active', item.dataset.view === state.view));
  document.querySelector('#view-crumb').textContent = labels[state.view] || (state.view === 'snapshots' ? 'Captured snapshots' : 'Dashboard');
  const renderers = { dashboard: renderDashboard, processes: renderProcesses, 'process-detail': renderProcessDetail, tree: renderTree, 'cpu-memory': renderCpuMemory, commands: renderCommands, concepts: renderConcepts, ipc: () => renderDemos('ipc'), memory: () => renderDemos('memory'), files: () => renderDemos('files'), threads: () => renderDemos('threads'), 'co-mapping': renderCoMapping, architecture: renderArchitecture, snapshots: renderSnapshots };
  page.innerHTML = (renderers[state.view] || renderDashboard)();
  attachPageEvents();
  if (focusedId === 'process-search') {
    const input = page.querySelector('#process-search');
    input?.focus();
    if (input && selection !== null) input.setSelectionRange(selection, selection);
  }
  drawCharts();
}

function attachPageEvents() {
  page.querySelectorAll('tr[data-pid]').forEach((row) => row.addEventListener('click', () => {
    state.selectedPid = Number(row.dataset.pid);
    state.view = 'process-detail';
    render();
  }));
  page.querySelector('#process-search')?.addEventListener('input', (event) => { state.search = event.target.value; render(); });
  page.querySelector('#process-sort')?.addEventListener('change', (event) => { state.sort = event.target.value; render(); });
  page.querySelectorAll('[data-remove-snapshot]').forEach((button) => button.addEventListener('click', () => {
    state.snapshots.splice(Number(button.dataset.removeSnapshot), 1);
    localStorage.setItem('linux-monitor-snapshots', JSON.stringify(state.snapshots));
    render();
  }));
}

function drawChart(canvasId, values, color, max = 100) {
  const canvas = document.getElementById(canvasId);
  if (!canvas) return;
  const bounds = canvas.getBoundingClientRect();
  const ratio = window.devicePixelRatio || 1;
  canvas.width = Math.max(1, Math.floor(bounds.width * ratio));
  canvas.height = Math.max(1, Math.floor(bounds.height * ratio));
  const context = canvas.getContext('2d');
  context.scale(ratio, ratio);
  const width = bounds.width;
  const height = bounds.height;
  const left = 28;
  const right = width - 8;
  const top = 10;
  const bottom = height - 20;
  context.font = '9px DM Mono, monospace';
  context.fillStyle = '#829083';
  context.strokeStyle = '#344138';
  [0, 0.5, 1].forEach((fraction) => {
    const y = bottom - fraction * (bottom - top);
    context.beginPath(); context.moveTo(left, y); context.lineTo(right, y); context.stroke();
    context.fillText(`${Math.round(max * fraction)}%`, 0, y + 3);
  });
  if (!values.length) {
    context.fillStyle = '#9aa99b'; context.fillText('Collecting real samples…', left + 8, (top + bottom) / 2);
    return;
  }
  const points = values.slice(-60);
  context.strokeStyle = color;
  context.lineWidth = 2;
  context.beginPath();
  points.forEach((value, index) => {
    const x = left + index * (right - left) / Math.max(1, points.length - 1);
    const y = bottom - Math.min(max, Math.max(0, value)) * (bottom - top) / max;
    if (index === 0) context.moveTo(x, y); else context.lineTo(x, y);
  });
  context.stroke();
}

function drawCharts() {
  drawChart('cpu-chart', state.history.map((item) => item.cpu), '#b7e36a');
  drawChart('memory-chart', state.history.map((item) => item.memory), '#78d8c0');
}

function download(filename, contents, type) {
  const link = document.createElement('a');
  link.href = URL.createObjectURL(new Blob([contents], { type }));
  link.download = filename;
  link.click();
  URL.revokeObjectURL(link.href);
}

function captureSnapshot() {
  if (!state.data) return;
  state.snapshots.unshift(state.data);
  state.snapshots = state.snapshots.slice(0, 30);
  localStorage.setItem('linux-monitor-snapshots', JSON.stringify(state.snapshots));
  state.view = 'snapshots';
  render();
}

function exportCsv() {
  if (!state.data) return;
  const quote = (value) => `"${String(value ?? '').replaceAll('"', '""')}"`;
  const lines = [['timestamp', 'pid', 'ppid', 'name', 'state', 'cpu_percent', 'memory_percent', 'memory_mb'], ...state.data.processes.map((proc) => [state.data.timestamp, proc.pid, proc.ppid, proc.name, proc.state, proc.cpu_percent, proc.memory_percent, proc.memory_mb])];
  download('linux-monitor-processes.csv', lines.map((line) => line.map(quote).join(',')).join('\n'), 'text/csv');
}

navItems.forEach((item) => item.addEventListener('click', () => { state.view = item.dataset.view; render(); }));
document.querySelector('#capture-button').addEventListener('click', captureSnapshot);
document.querySelector('#export-json-button').addEventListener('click', () => { if (state.data) download('linux-monitor-snapshot.json', JSON.stringify(state.data, null, 2), 'application/json'); });
document.querySelector('#export-csv-button').addEventListener('click', exportCsv);

async function refresh() {
  try {
    const response = await fetch(`data.json?_=${Date.now()}`, { cache: 'no-store' });
    if (!response.ok) throw new Error('collector has not written data.json');
    const incoming = await response.json();
    if (incoming.mode !== 'LIVE' || !Array.isArray(incoming.processes) || !incoming.system) throw new Error('invalid live snapshot');
    state.data = incoming;
    state.history.push({ cpu: Number(incoming.system.cpu_usage_pct) || 0, memory: Number(incoming.system.memory_usage_pct) || 0 });
    state.history = state.history.slice(-60);
    const ageSeconds = (Date.now() - new Date(incoming.timestamp).getTime()) / 1000;
    setConnection(ageSeconds <= 10 ? 'live' : 'stale');
  } catch {
    setConnection('offline');
  }
  render();
}

window.addEventListener('resize', drawCharts);
refresh();
window.setInterval(refresh, 2000);