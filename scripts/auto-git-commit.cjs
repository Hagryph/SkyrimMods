const { spawnSync } = require('node:child_process');
const path = require('node:path');

const repoRoot = path.resolve(__dirname, '..');
const message = process.env.BUILD_COMMIT_MESSAGE || 'chore: update build';

const run = (args) =>
  spawnSync('git', args, {
    cwd: repoRoot,
    encoding: 'utf8',
    stdio: ['ignore', 'pipe', 'pipe'],
  });

const outputOf = (result) => [result.stdout, result.stderr].filter(Boolean).join('\n').trim();

const fail = (result, action) => {
  if (result.status === 0) return;
  console.error(`auto-git-commit: failed to ${action}`);
  const output = outputOf(result);
  if (output) console.error(output);
  process.exit(result.status || 1);
};

const status = run(['status', '--short']);
fail(status, 'read git status');

if (!run(['config', 'user.name']).stdout.trim()) {
  fail(run(['config', 'user.name', 'Hagryph']), 'set local git user.name');
}
if (!run(['config', 'user.email']).stdout.trim()) {
  fail(run(['config', 'user.email', 'hagryph.gaming@gmail.com']), 'set local git user.email');
}

const conflicts = run(['diff', '--name-only', '--diff-filter=U']);
fail(conflicts, 'check for merge conflicts');
const conflictedFiles = conflicts.stdout
  .split(/\r?\n/)
  .map((line) => line.trim())
  .filter(Boolean);

if (conflictedFiles.length) {
  console.error('');
  console.error('auto-git-commit: unresolved merge conflicts found; no build commit was made');
  console.error(`Conflicted files:\n${conflictedFiles.map((file) => `- ${file}`).join('\n')}`);
  console.error('');
  process.exit(1);
}

fail(run(['add', '-A']), 'stage changes');

const diff = run(['diff', '--cached', '--quiet']);
if (diff.status === 0) {
  console.log('auto-git-commit: no staged changes to commit');
  process.exit(0);
}
if (diff.status !== 1) fail(diff, 'check staged changes');

const commit = run(['commit', '-m', message]);
if (commit.status === 0) {
  process.stdout.write(commit.stdout);
  process.exit(0);
}

const output = outputOf(commit);
if (/nothing to commit|no changes added to commit/i.test(output)) {
  console.log('auto-git-commit: no changes to commit');
  process.exit(0);
}

const refreshedStatus = run(['status', '--short']);
console.error('');
console.error('auto-git-commit: git commit failed; no build commit was made');
console.error(`Git output:\n${output || '(no output)'}`);
console.error(`\nCurrent git status:\n${outputOf(refreshedStatus) || '(clean)'}`);
console.error('');
process.exit(commit.status || 1);
