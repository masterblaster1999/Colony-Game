#!/usr/bin/env node
/**
 * gh-browse.js
 *
 * One-file interactive CLI to browse GitHub *public* repositories.
 *
 * - No dependencies (Node.js 18+ recommended).
 * - Uses GitHub REST API: search, contents, branches, and git trees.
 * - Optional auth via env var GITHUB_TOKEN / GH_TOKEN / GITHUB_PAT.
 */

'use strict';

const readline = require('node:readline');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const { exec: execCb } = require('node:child_process');
const { Readable } = require('node:stream');
const { pipeline } = require('node:stream/promises');

// --------- Config ---------

const API_BASE = 'https://api.github.com';
const API_VERSION = '2022-11-28';

const TOKEN = process.env.GITHUB_TOKEN || process.env.GH_TOKEN || process.env.GITHUB_PAT || '';
const USER_AGENT = process.env.GH_BROWSE_USER_AGENT
  || `gh-browse-onefile (${os.userInfo().username || 'user'})`;

// Prevent huge dumps to the terminal.
const MAX_CAT_BYTES = Number(process.env.GH_BROWSE_CAT_BYTES || 200 * 1024); // 200 KiB
const MAX_FIND_RESULTS = Number(process.env.GH_BROWSE_FIND_RESULTS || 200);
const MAX_TREE_LINES = Number(process.env.GH_BROWSE_TREE_LINES || 4000);

// Basic in-memory cache (ETag-aware) so navigation doesn't spam the API.
const CACHE_TTL_MS = Number(process.env.GH_BROWSE_CACHE_TTL_MS || 5 * 60 * 1000);

// --------- Helpers ---------

class ApiError extends Error {
  /**
   * @param {string} message
   * @param {{status?: number, url?: string, info?: any, rate?: any}} meta
   */
  constructor(message, meta = {}) {
    super(message);
    this.name = 'ApiError';
    this.status = meta.status;
    this.url = meta.url;
    this.info = meta.info;
    this.rate = meta.rate;
  }
}

function nowMs() {
  return Date.now();
}

function clamp(n, min, max) {
  return Math.max(min, Math.min(max, n));
}

function humanBytes(bytes) {
  if (!Number.isFinite(bytes)) return '';
  const units = ['B', 'KiB', 'MiB', 'GiB', 'TiB'];
  let b = Math.max(0, bytes);
  let i = 0;
  while (b >= 1024 && i < units.length - 1) {
    b /= 1024;
    i++;
  }
  const digits = i === 0 ? 0 : i === 1 ? 1 : 2;
  return `${b.toFixed(digits)} ${units[i]}`;
}

function padRight(s, n) {
  const str = String(s);
  return str.length >= n ? str : str + ' '.repeat(n - str.length);
}

function splitArgs(line) {
  // Simple argv parser: supports "..." and '...'
  const re = /"([^"]*)"|'([^']*)'|(\S+)/g;
  const out = [];
  let m;
  while ((m = re.exec(line)) !== null) {
    out.push(m[1] ?? m[2] ?? m[3]);
  }
  return out;
}

function encodePathSegments(p) {
  // Encode each segment, but keep slashes.
  return p.split('/').map(seg => encodeURIComponent(seg)).join('/');
}

function normalizeRepoPath(p) {
  // Always return a path relative to the repo root, without leading slash.
  const raw = String(p || '').replace(/\\/g, '/');
  const parts = raw.split('/').filter(Boolean);
  const stack = [];
  for (const part of parts) {
    if (part === '.') continue;
    if (part === '..') {
      stack.pop();
      continue;
    }
    stack.push(part);
  }
  return stack.join('/');
}

function joinRepoPath(a, b) {
  return normalizeRepoPath([a, b].filter(Boolean).join('/'));
}

function looksBinary(buf) {
  // Heuristic: if there are NUL bytes or a high proportion of non-text bytes.
  if (!buf || buf.length === 0) return false;
  const sample = buf.subarray(0, Math.min(buf.length, 8192));
  let weird = 0;
  for (const byte of sample) {
    if (byte === 0) return true;
    // allow common whitespace
    if (byte === 9 || byte === 10 || byte === 13) continue;
    // printable ASCII range
    if (byte >= 32 && byte <= 126) continue;
    // UTF-8 bytes may be > 126; don't count them as weird too aggressively
    if (byte >= 127) {
      weird += 0.3;
      continue;
    }
    // control chars
    weird += 1;
  }
  return weird / sample.length > 0.15;
}

function formatIsoFromEpochSeconds(epochSeconds) {
  const n = Number(epochSeconds);
  if (!Number.isFinite(n) || n <= 0) return null;
  const d = new Date(n * 1000);
  return d.toISOString();
}

function parseRate(headers) {
  const getNum = (k) => {
    const v = headers.get(k);
    if (v == null) return null;
    const n = Number(v);
    return Number.isFinite(n) ? n : null;
  };
  const limit = getNum('x-ratelimit-limit');
  const remaining = getNum('x-ratelimit-remaining');
  const used = getNum('x-ratelimit-used');
  const reset = getNum('x-ratelimit-reset');
  const retryAfter = headers.get('retry-after');
  const retryAfterSeconds = retryAfter != null ? Number(retryAfter) : null;
  return { limit, remaining, used, reset, retryAfterSeconds };
}

function describeRate(rate) {
  if (!rate) return 'rate: (unknown)';
  const parts = [];
  if (rate.remaining != null && rate.limit != null) {
    parts.push(`remaining ${rate.remaining}/${rate.limit}`);
  } else if (rate.remaining != null) {
    parts.push(`remaining ${rate.remaining}`);
  }
  if (rate.reset != null) {
    const iso = formatIsoFromEpochSeconds(rate.reset);
    if (iso) parts.push(`resets ${iso}`);
  }
  if (Number.isFinite(rate.retryAfterSeconds) && rate.retryAfterSeconds > 0) {
    parts.push(`retry-after ${rate.retryAfterSeconds}s`);
  }
  return parts.length ? `rate: ${parts.join(' · ')}` : 'rate: (unknown)';
}

// --------- GitHub API client (ETag cache + basic rate-limit messaging) ---------

/** @type {Map<string, {etag?: string, ts: number, data: any}>} */
const etagCache = new Map();
let lastRate = null;

async function ghRequest(url, {
  accept = 'application/vnd.github+json',
  response = 'json', // 'json' | 'text' | 'buffer'
  method = 'GET',
  noCache = false,
} = {}) {
  const headers = {
    'Accept': accept,
    'X-GitHub-Api-Version': API_VERSION,
    'User-Agent': USER_AGENT,
  };
  if (TOKEN) headers['Authorization'] = `Bearer ${TOKEN}`;

  const cacheKey = `${method} ${url} :: ${accept}`;
  const cached = etagCache.get(cacheKey);
  if (!noCache && cached && (nowMs() - cached.ts) < CACHE_TTL_MS && cached.etag) {
    headers['If-None-Match'] = cached.etag;
  }

  let res;
  try {
    res = await fetch(url, { method, headers, redirect: 'follow' });
  } catch (err) {
    throw new ApiError(`Network error while fetching ${url}: ${err.message}`);
  }

  lastRate = parseRate(res.headers);

  if (res.status === 304 && cached) {
    return cached.data;
  }

  const contentType = (res.headers.get('content-type') || '').toLowerCase();

  let data;
  try {
    if (response === 'buffer') {
      // read as ArrayBuffer and wrap
      const ab = await res.arrayBuffer();
      data = Buffer.from(ab);
    } else if (response === 'text' || !contentType.includes('application/json')) {
      data = await res.text();
    } else {
      data = await res.json();
    }
  } catch (err) {
    // Fallback to text if JSON parsing fails.
    try {
      data = await res.text();
    } catch (_) {
      data = null;
    }
  }

  if (!res.ok) {
    // Add better guidance for common GitHub rate limit responses.
    const msg = (() => {
      const maybeMessage = (data && typeof data === 'object' && data.message) ? data.message : null;
      if (res.status === 403 || res.status === 429) {
        if (lastRate && lastRate.remaining === 0 && lastRate.reset) {
          const iso = formatIsoFromEpochSeconds(lastRate.reset);
          return `${maybeMessage || 'Rate limit hit.'} (reset at ${iso || lastRate.reset})`;
        }
        if (lastRate && Number.isFinite(lastRate.retryAfterSeconds) && lastRate.retryAfterSeconds > 0) {
          return `${maybeMessage || 'Secondary rate limit hit.'} (retry after ${lastRate.retryAfterSeconds}s)`;
        }
      }
      return maybeMessage || `HTTP ${res.status} ${res.statusText}`;
    })();

    throw new ApiError(msg, { status: res.status, url, info: data, rate: lastRate });
  }

  const etag = res.headers.get('etag');
  if (!noCache && etag) {
    etagCache.set(cacheKey, { etag, ts: nowMs(), data });
  } else if (!noCache) {
    etagCache.set(cacheKey, { ts: nowMs(), data });
  }

  return data;
}

// --------- App state ---------

const state = {
  owner: null,
  repo: null,
  branch: null,
  cwd: '',
  repoInfo: null,
  lastSearch: null, // array of repo items
  tree: null,       // raw tree response from /git/trees
  treeIndex: null,  // Map<string, {dirs:Set<string>, files:Set<string>, meta:Map<string, any>}>
};

function hasRepo() {
  return Boolean(state.owner && state.repo);
}

function promptLabel() {
  if (!hasRepo()) return 'gh> ';
  const br = state.branch ? `@${state.branch}` : '';
  const cwd = state.cwd ? `/${state.cwd}` : '/';
  return `${state.owner}/${state.repo}${br}:${cwd}> `;
}

function printHelp() {
  console.log(`
Commands:
  help

  # discover repos
  search <query>                 Search public repos (shows top 10)
  use <n|owner/repo|url>          Use a repo from last search by number, or set directly

  # navigation
  info                            Show current repo/branch/path
  branches                        List branches
  branch <name>                   Switch branch (resets path to /)
  ls [path]                       List directory (default: current)
  cd <dir|..|/>                   Change directory
  pwd                             Print current path

  # files
  cat <file>                      Print file (preview; detects binary)
  download <file> [dest]          Download file to disk

  # faster browsing
  tree [depth]                    Fetch repo tree (recursive) and print a tree view
  find <substring>                Find paths in cached tree (run 'tree' first)

  # misc
  url [path]                      Print GitHub web URL for current dir or a file/dir
  web [path]                      Open that URL in your default browser (best effort)
  rate                            Show last seen API rate headers
  clear                           Clear the screen
  quit | exit
`);
}

function parseRepoSpec(input) {
  const s = String(input || '').trim();
  if (!s) return null;

  // Accept:
  // - owner/repo
  // - https://github.com/owner/repo
  // - https://github.com/owner/repo/tree/branch/path
  // - git@github.com:owner/repo.git

  // git@github.com:owner/repo.git
  const mSsh = s.match(/^git@github\.com:([^/\s]+)\/([^\s]+?)(?:\.git)?$/i);
  if (mSsh) return { owner: mSsh[1], repo: mSsh[2] };

  // https?://github.com/owner/repo...
  const mHttp = s.match(/^https?:\/\/github\.com\/([^/\s]+)\/([^/\s#?]+)(?:[\/#?].*)?$/i);
  if (mHttp) return { owner: mHttp[1], repo: mHttp[2].replace(/\.git$/i, '') };

  // owner/repo
  const mShort = s.match(/^([^/\s]+)\/([^/\s]+)$/);
  if (mShort) return { owner: mShort[1], repo: mShort[2].replace(/\.git$/i, '') };

  return null;
}

async function setRepo(owner, repo) {
  const o = String(owner).trim();
  const r = String(repo).trim();
  if (!o || !r) throw new Error('Repo must be in the form owner/repo.');

  const url = `${API_BASE}/repos/${encodeURIComponent(o)}/${encodeURIComponent(r)}`;
  const info = await ghRequest(url);

  state.owner = o;
  state.repo = r;
  state.repoInfo = info;
  state.branch = info.default_branch || 'main';
  state.cwd = '';
  state.tree = null;
  state.treeIndex = null;

  console.log(`\nNow browsing: ${info.full_name} (default branch: ${state.branch})`);
  if (info.description) console.log(info.description);
  if (info.html_url) console.log(info.html_url);
}

function resolveRepoPath(input) {
  const raw = String(input || '').trim();
  if (!raw || raw === '.') return state.cwd;
  if (raw === '/') return '';

  if (raw.startsWith('/')) {
    return normalizeRepoPath(raw.slice(1));
  }
  return normalizeRepoPath(joinRepoPath(state.cwd, raw));
}

function contentsUrl(repoPath) {
  const base = `${API_BASE}/repos/${encodeURIComponent(state.owner)}/${encodeURIComponent(state.repo)}/contents`;
  const p = normalizeRepoPath(repoPath);
  const pathPart = p ? `/${encodePathSegments(p)}` : '';
  const refPart = state.branch ? `?ref=${encodeURIComponent(state.branch)}` : '';
  return `${base}${pathPart}${refPart}`;
}

function branchesUrl() {
  return `${API_BASE}/repos/${encodeURIComponent(state.owner)}/${encodeURIComponent(state.repo)}/branches?per_page=100`;
}

function branchUrl(branchName) {
  // Branch names can include slashes, so encode the whole thing.
  return `${API_BASE}/repos/${encodeURIComponent(state.owner)}/${encodeURIComponent(state.repo)}/branches/${encodeURIComponent(branchName)}`;
}

function treeUrl(treeSha, recursive = true) {
  const rec = recursive ? '?recursive=1' : '';
  return `${API_BASE}/repos/${encodeURIComponent(state.owner)}/${encodeURIComponent(state.repo)}/git/trees/${encodeURIComponent(treeSha)}${rec}`;
}

function webUrlForPath(repoPath, kind /* 'file'|'dir' */) {
  const p = normalizeRepoPath(repoPath);
  const br = state.branch || (state.repoInfo && state.repoInfo.default_branch) || 'main';
  if (!p) return `https://github.com/${state.owner}/${state.repo}/tree/${encodeURIComponent(br)}`;
  const encoded = p.split('/').map(encodeURIComponent).join('/');
  const base = kind === 'file' ? 'blob' : 'tree';
  return `https://github.com/${state.owner}/${state.repo}/${base}/${encodeURIComponent(br)}/${encoded}`;
}

async function getContents(repoPath) {
  if (!hasRepo()) throw new Error('No repo selected. Use `search ...` then `use <n>`, or `use owner/repo`.');
  const url = contentsUrl(repoPath);
  return ghRequest(url);
}

function printEntries(entries) {
  const rows = entries.map(e => {
    const type = e.type === 'dir' ? 'DIR' : e.type === 'file' ? 'FILE' : (e.type || '').toUpperCase();
    const size = e.size != null ? humanBytes(e.size) : '';
    return { type, name: e.name, size };
  });

  const typeW = Math.max(4, ...rows.map(r => r.type.length));
  const sizeW = Math.max(4, ...rows.map(r => r.size.length));

  for (const r of rows) {
    console.log(`${padRight(r.type, typeW)}  ${padRight(r.size, sizeW)}  ${r.name}`);
  }
}

async function cmdLs(arg) {
  const p = resolveRepoPath(arg);
  const data = await getContents(p);

  if (Array.isArray(data)) {
    console.log(`\nListing ${p ? '/' + p : '/'}:`);
    // dirs first
    const dirs = data.filter(e => e.type === 'dir').sort((a, b) => a.name.localeCompare(b.name));
    const files = data.filter(e => e.type !== 'dir').sort((a, b) => a.name.localeCompare(b.name));
    printEntries([...dirs, ...files]);
    return;
  }

  // File response
  if (data && typeof data === 'object') {
    console.log(`\n${data.type || 'item'}: ${data.path}`);
    if (data.size != null) console.log(`size: ${humanBytes(data.size)}`);
    if (data.html_url) console.log(`url: ${data.html_url}`);
    if (data.type === 'file') console.log(`Tip: cat ${JSON.stringify(data.name)}`);
    return;
  }

  console.log('Unknown response.');
}

async function cmdCd(arg) {
  if (!hasRepo()) throw new Error('No repo selected.');
  if (!arg) {
    console.log(state.cwd ? '/' + state.cwd : '/');
    return;
  }

  if (arg === '/' || arg === '\\') {
    state.cwd = '';
    console.log('/');
    return;
  }

  const next = resolveRepoPath(arg);

  // Validate it's a directory by checking contents.
  const data = await getContents(next);
  if (!Array.isArray(data)) {
    throw new Error(`${arg} is not a directory.`);
  }

  state.cwd = next;
  console.log(state.cwd ? '/' + state.cwd : '/');
}

async function cmdCat(fileArg) {
  if (!fileArg) throw new Error('Usage: cat <file>');
  const p = resolveRepoPath(fileArg);
  const meta = await getContents(p);

  if (!meta || typeof meta !== 'object' || Array.isArray(meta)) {
    throw new Error('That path is not a file.');
  }
  if (meta.type !== 'file') {
    throw new Error(`That path is a ${meta.type}, not a file.`);
  }

  const size = Number(meta.size);
  if (Number.isFinite(size) && size > MAX_CAT_BYTES) {
    console.log(`\nFile is ${humanBytes(size)}; showing first ${humanBytes(MAX_CAT_BYTES)} (use download for full).\n`);
  } else {
    console.log('');
  }

  const url = contentsUrl(p);

  // Fetch raw bytes, but only up to MAX_CAT_BYTES.
  const headers = {
    'Accept': 'application/vnd.github.raw+json',
    'X-GitHub-Api-Version': API_VERSION,
    'User-Agent': USER_AGENT,
  };
  if (TOKEN) headers['Authorization'] = `Bearer ${TOKEN}`;

  const res = await fetch(url, { headers, redirect: 'follow' });
  lastRate = parseRate(res.headers);

  if (!res.ok) {
    let t = '';
    try { t = await res.text(); } catch (_) {}
    throw new ApiError(`HTTP ${res.status} ${res.statusText}`, { status: res.status, url, info: t, rate: lastRate });
  }

  if (!res.body) {
    const txt = await res.text();
    process.stdout.write(txt);
    return;
  }

  const reader = res.body.getReader();
  const chunks = [];
  let received = 0;
  let done = false;
  while (!done && received < MAX_CAT_BYTES) {
    const r = await reader.read();
    done = r.done;
    if (r.value) {
      const buf = Buffer.from(r.value);
      const remain = MAX_CAT_BYTES - received;
      if (buf.length > remain) {
        chunks.push(buf.subarray(0, remain));
        received += remain;
        break;
      }
      chunks.push(buf);
      received += buf.length;
    }
  }
  try { await reader.cancel(); } catch (_) {}

  const outBuf = Buffer.concat(chunks, received);
  if (looksBinary(outBuf)) {
    console.log('[binary file] Use: download <file> [dest]');
    return;
  }

  process.stdout.write(outBuf.toString('utf8'));
  if (Number.isFinite(size) && size > MAX_CAT_BYTES) {
    console.log(`\n\n[truncated at ${humanBytes(MAX_CAT_BYTES)} of ${humanBytes(size)}]`);
  }
}

async function cmdDownload(fileArg, destArg) {
  if (!fileArg) throw new Error('Usage: download <file> [dest]');
  const p = resolveRepoPath(fileArg);
  const meta = await getContents(p);

  if (!meta || typeof meta !== 'object' || Array.isArray(meta) || meta.type !== 'file') {
    throw new Error('That path is not a file.');
  }

  const downloadUrl = meta.download_url;
  if (!downloadUrl) {
    throw new Error('No download_url available for that file.');
  }

  const dest = destArg
    ? path.resolve(destArg)
    : path.resolve(process.cwd(), path.basename(p));

  console.log(`Downloading to: ${dest}`);

  const res = await fetch(downloadUrl, { redirect: 'follow', headers: { 'User-Agent': USER_AGENT } });
  if (!res.ok) {
    throw new Error(`Download failed: HTTP ${res.status} ${res.statusText}`);
  }

  if (!res.body) {
    const ab = await res.arrayBuffer();
    fs.writeFileSync(dest, Buffer.from(ab));
    console.log('Done.');
    return;
  }

  await pipeline(Readable.fromWeb(res.body), fs.createWriteStream(dest));
  console.log('Done.');
}

async function cmdSearch(query) {
  const q = String(query || '').trim();
  if (!q) throw new Error('Usage: search <query>');

  // Search API: keep results small to be nice to the rate limit.
  const url = `${API_BASE}/search/repositories?q=${encodeURIComponent(q)}&per_page=10&page=1`;
  const data = await ghRequest(url);

  const items = Array.isArray(data && data.items) ? data.items : [];
  state.lastSearch = items;

  if (!items.length) {
    console.log('No results.');
    return;
  }

  console.log(`\nTop results for: ${q}`);
  items.forEach((repo, i) => {
    const stars = repo.stargazers_count != null ? `⭐ ${repo.stargazers_count}` : '';
    const desc = repo.description ? ` — ${repo.description}` : '';
    console.log(`${String(i + 1).padStart(2, ' ')}. ${repo.full_name} ${stars}${desc}`);
  });
  console.log('\nUse one with: use <number>');
}

async function cmdUse(spec) {
  const s = String(spec || '').trim();
  if (!s) throw new Error('Usage: use <n|owner/repo|url>');

  // number from last search
  const n = Number(s);
  if (Number.isInteger(n) && n >= 1 && n <= (state.lastSearch ? state.lastSearch.length : 0)) {
    const item = state.lastSearch[n - 1];
    const full = item.full_name;
    const [owner, repo] = String(full).split('/');
    await setRepo(owner, repo);
    return;
  }

  const parsed = parseRepoSpec(s);
  if (!parsed) throw new Error('Could not parse repo. Try: use owner/repo');
  await setRepo(parsed.owner, parsed.repo);
}

async function cmdBranches() {
  if (!hasRepo()) throw new Error('No repo selected.');
  const data = await ghRequest(branchesUrl());
  if (!Array.isArray(data)) {
    console.log('Unexpected response.');
    return;
  }
  const names = data.map(b => b.name).sort((a, b) => a.localeCompare(b));
  console.log(`\nBranches (${names.length}):`);
  for (const name of names) {
    console.log(name === state.branch ? `* ${name}` : `  ${name}`);
  }
}

async function cmdBranch(nameArg) {
  if (!hasRepo()) throw new Error('No repo selected.');
  const name = String(nameArg || '').trim();
  if (!name) {
    console.log(state.branch ? `Current branch: ${state.branch}` : 'No branch set.');
    console.log('Tip: branches  (to list)');
    return;
  }

  // Validate branch exists by calling Get a branch.
  await ghRequest(branchUrl(name));

  state.branch = name;
  state.cwd = '';
  state.tree = null;
  state.treeIndex = null;
  console.log(`Switched to branch: ${name}`);
}

function buildTreeIndex(treeResp) {
  const index = new Map();
  const meta = new Map();

  const ensureDir = (dir) => {
    const key = dir || '';
    if (!index.has(key)) index.set(key, { dirs: new Set(), files: new Set() });
    return index.get(key);
  };

  ensureDir('');

  const entries = Array.isArray(treeResp && treeResp.tree) ? treeResp.tree : [];
  for (const e of entries) {
    if (!e || !e.path) continue;
    const fullPath = normalizeRepoPath(e.path);
    meta.set(fullPath, e);

    const parts = fullPath.split('/').filter(Boolean);
    let dir = '';
    for (let i = 0; i < parts.length; i++) {
      const name = parts[i];
      const isLeaf = (i === parts.length - 1);
      const node = ensureDir(dir);
      if (isLeaf) {
        if (e.type === 'tree') node.dirs.add(name);
        else node.files.add(name);
      } else {
        node.dirs.add(name);
      }
      dir = joinRepoPath(dir, name);
    }
  }

  // Stash file metadata on the index object.
  index.__meta = meta;
  return index;
}

function printTreeFromIndex(dir, depth) {
  const index = state.treeIndex;
  if (!index) {
    console.log('No tree cached. Run: tree');
    return;
  }

  let lines = 0;

  const walk = (d, pref, remainingDepth) => {
    if (lines >= MAX_TREE_LINES) return;
    const node = index.get(d);
    if (!node) return;

    const dirs = Array.from(node.dirs).sort((a, b) => a.localeCompare(b));
    const files = Array.from(node.files).sort((a, b) => a.localeCompare(b));
    const entries = [
      ...dirs.map(name => ({ name, kind: 'dir' })),
      ...files.map(name => ({ name, kind: 'file' })),
    ];

    for (let i = 0; i < entries.length; i++) {
      if (lines >= MAX_TREE_LINES) return;
      const e = entries[i];
      const last = i === entries.length - 1;
      const branch = last ? '└─ ' : '├─ ';
      const nextPref = pref + (last ? '   ' : '│  ');
      console.log(pref + branch + e.name + (e.kind === 'dir' ? '/' : ''));
      lines++;

      if (e.kind === 'dir' && remainingDepth > 1) {
        const childDir = joinRepoPath(d, e.name);
        walk(childDir, nextPref, remainingDepth - 1);
      }
    }
  };

  const rootLabel = dir ? `/${dir}` : '/';
  console.log(`\n${rootLabel}`);
  walk(dir, '', depth);

  if (lines >= MAX_TREE_LINES) {
    console.log(`\n[tree output truncated at ${MAX_TREE_LINES} lines]`);
  }
}

async function cmdTree(depthArg) {
  if (!hasRepo()) throw new Error('No repo selected.');
  let depth = Number(depthArg);
  if (!Number.isFinite(depth) || depth <= 0) depth = 4;
  depth = clamp(Math.floor(depth), 1, 20);

  if (!state.tree || !state.treeIndex) {
    console.log('Fetching repo tree (recursive)…');

    // Step 1: get branch info (includes commit.commit.tree.sha)
    const br = await ghRequest(branchUrl(state.branch));
    const treeSha = br && br.commit && br.commit.commit && br.commit.commit.tree && br.commit.commit.tree.sha;
    if (!treeSha) {
      throw new Error('Could not find tree SHA for this branch.');
    }

    // Step 2: fetch git tree recursively
    const tr = await ghRequest(treeUrl(treeSha, true));
    state.tree = tr;
    state.treeIndex = buildTreeIndex(tr);

    const count = Array.isArray(tr && tr.tree) ? tr.tree.length : 0;
    console.log(`Tree cached (${count} entries).${tr && tr.truncated ? ' [TRUNCATED]' : ''}`);
  }

  printTreeFromIndex(state.cwd, depth);
}

async function cmdFind(substr) {
  if (!hasRepo()) throw new Error('No repo selected.');
  const q = String(substr || '').trim();
  if (!q) throw new Error('Usage: find <substring>');
  if (!state.treeIndex) {
    console.log('No tree cached yet. Run: tree');
    return;
  }

  const meta = state.treeIndex.__meta;
  const cwdPrefix = state.cwd ? `${state.cwd}/` : '';
  const needle = q.toLowerCase();

  let shown = 0;
  for (const p of meta.keys()) {
    if (!p.startsWith(cwdPrefix)) continue;
    if (p.toLowerCase().includes(needle)) {
      console.log(p);
      shown++;
      if (shown >= MAX_FIND_RESULTS) break;
    }
  }

  if (!shown) console.log('No matches.');
  else if (shown >= MAX_FIND_RESULTS) console.log(`[truncated at ${MAX_FIND_RESULTS} results]`);
}

async function cmdUrl(pathArg) {
  if (!hasRepo()) throw new Error('No repo selected.');
  const p = resolveRepoPath(pathArg);

  // Best effort: try to detect file/dir without extra request.
  let kind = 'dir';
  if (p) {
    if (state.treeIndex) {
      const meta = state.treeIndex.__meta;
      const e = meta.get(p);
      if (e && e.type && e.type !== 'tree') kind = 'file';
    } else {
      // Fallback to a single contents call.
      try {
        const data = await getContents(p);
        kind = Array.isArray(data) ? 'dir' : 'file';
      } catch (_) {
        // If it fails, just guess file.
        kind = 'file';
      }
    }
  }

  console.log(webUrlForPath(p, kind));
}

function openInBrowser(url) {
  const plat = process.platform;
  if (plat === 'win32') {
    // cmd.exe built-in
    execCb(`cmd /c start "" "${url}"`);
    return;
  }
  if (plat === 'darwin') {
    execCb(`open "${url}"`);
    return;
  }
  // Linux / others
  execCb(`xdg-open "${url}"`);
}

async function cmdWeb(pathArg) {
  if (!hasRepo()) throw new Error('No repo selected.');
  const p = resolveRepoPath(pathArg);

  let kind = 'dir';
  if (p) {
    if (state.treeIndex) {
      const e = state.treeIndex.__meta.get(p);
      if (e && e.type && e.type !== 'tree') kind = 'file';
    } else {
      try {
        const data = await getContents(p);
        kind = Array.isArray(data) ? 'dir' : 'file';
      } catch (_) {
        kind = 'file';
      }
    }
  }

  const url = webUrlForPath(p, kind);
  console.log(url);
  console.log('(opening browser…)');
  openInBrowser(url);
}

function cmdInfo() {
  if (!hasRepo()) {
    console.log('No repo selected.');
    return;
  }
  console.log(`repo:   ${state.owner}/${state.repo}`);
  console.log(`branch: ${state.branch}`);
  console.log(`path:   ${state.cwd ? '/' + state.cwd : '/'}`);
  if (state.repoInfo && state.repoInfo.html_url) console.log(`web:    ${state.repoInfo.html_url}`);
}

function cmdRate() {
  console.log(describeRate(lastRate));
}

function cmdClear() {
  process.stdout.write('\x1Bc');
}

// --------- CLI loop ---------

async function main() {
  if (typeof fetch !== 'function') {
    console.error('This tool needs Node.js 18+ (global fetch).');
    process.exitCode = 1;
    return;
  }

  console.log('gh-browse (one-file) — GitHub public repo browser');
  console.log('Type `help` for commands. Tip: `search react router` then `use 1`.');
  if (!TOKEN) {
    console.log('No token detected (GITHUB_TOKEN/GH_TOKEN). Public browsing works, but you may hit lower rate limits.');
  }

  const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout,
    terminal: true,
  });

  const reprompt = () => {
    rl.setPrompt(promptLabel());
    rl.prompt();
  };

  rl.on('SIGINT', () => {
    console.log('\n(Use `exit` to quit.)');
    reprompt();
  });

  rl.on('close', () => {
    console.log('bye');
    process.exit(0);
  });

  reprompt();

  rl.on('line', async (line) => {
    const trimmed = String(line || '').trim();
    if (!trimmed) return reprompt();

    const argv = splitArgs(trimmed);
    const cmd = (argv[0] || '').toLowerCase();
    const args = argv.slice(1);

    rl.pause();
    try {
      if (cmd === 'help' || cmd === '?') {
        printHelp();
      } else if (cmd === 'search') {
        await cmdSearch(args.join(' '));
      } else if (cmd === 'use') {
        await cmdUse(args[0]);
      } else if (cmd === 'info') {
        cmdInfo();
      } else if (cmd === 'branches') {
        await cmdBranches();
      } else if (cmd === 'branch') {
        await cmdBranch(args[0]);
      } else if (cmd === 'ls' || cmd === 'dir') {
        await cmdLs(args[0]);
      } else if (cmd === 'cd') {
        await cmdCd(args[0]);
      } else if (cmd === 'pwd') {
        console.log(state.cwd ? '/' + state.cwd : '/');
      } else if (cmd === 'cat') {
        await cmdCat(args[0]);
      } else if (cmd === 'download' || cmd === 'dl') {
        await cmdDownload(args[0], args[1]);
      } else if (cmd === 'tree') {
        await cmdTree(args[0]);
      } else if (cmd === 'find') {
        await cmdFind(args.join(' '));
      } else if (cmd === 'url') {
        await cmdUrl(args[0]);
      } else if (cmd === 'web') {
        await cmdWeb(args[0]);
      } else if (cmd === 'rate') {
        cmdRate();
      } else if (cmd === 'clear' || cmd === 'cls') {
        cmdClear();
      } else if (cmd === 'exit' || cmd === 'quit' || cmd === 'q') {
        rl.close();
        return;
      } else {
        console.log(`Unknown command: ${cmd}. Type 'help'.`);
      }
    } catch (err) {
      if (err instanceof ApiError) {
        console.error(`Error: ${err.message}`);
        if (err.url) console.error(`URL: ${err.url}`);
        if (err.rate) console.error(describeRate(err.rate));
      } else {
        console.error(`Error: ${err.message || err}`);
      }
    } finally {
      rl.resume();
      reprompt();
    }
  });
}

main().catch((e) => {
  console.error(e);
  process.exitCode = 1;
});
