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

const requestInput = (title, details, options = []) => {
  console.error('');
  console.error(`auto-git-commit: ${title}`);
  if (details) console.error(details);
  if (options.length) {
    console.error('');
    console.error('Input required. Choose what to do:');
    for (const option of options) console.error(`- ${option}`);
  }
  console.error('');
};

const fail = (result, action) => {
  if (result.status === 0) return false;
  const stderr = result.stderr.trim();
  const stdout = result.stdout.trim();
  console.error(`auto-git-commit: failed to ${action}`);
  if (stderr) console.error(stderr);
  if (stdout) console.error(stdout);
  process.exit(result.status || 1);
};

const pushCurrentBranch = () => {
  const branch = run(['rev-parse', '--abbrev-ref', 'HEAD']);
  fail(branch, 'read current git branch');
  const name = branch.stdout.trim();
  if (!name || name === 'HEAD') {
    console.log('auto-git-commit: detached HEAD; skipped push');
    return;
  }

  const upstream = run(['rev-parse', '--abbrev-ref', '--symbolic-full-name', '@{u}']);
  const push = upstream.status === 0 ? run(['push']) : run(['push', '-u', 'origin', name]);
  fail(push, 'push build commit');
  const output = outputOf(push);
  if (output) process.stdout.write(`${output}\n`);
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
  requestInput(
    'unresolved merge conflicts found; no build commit was made',
    `Conflicted files:\n${conflictedFiles.map((file) => `- ${file}`).join('\n')}\n\nCurrent git status:\n${status.stdout.trim() || '(clean)'}`,
    [
      'Resolve the conflicts, then rerun the build script.',
      'Commit manually if the automatic build commit should be skipped.',
    ],
  );
  process.exit(1);
}

fail(run(['add', '-A']), 'stage project changes');

const diff = run(['diff', '--cached', '--quiet']);
if (diff.status === 0) {
  console.log('auto-git-commit: no staged changes to commit');
  process.exit(0);
}
if (diff.status !== 1) fail(diff, 'check staged changes');

const commit = run(['commit', '-m', message]);
if (commit.status === 0) {
  process.stdout.write(commit.stdout);
  pushCurrentBranch();
  process.exit(0);
}

const output = `${commit.stdout}\n${commit.stderr}`;
if (/nothing to commit|no changes added to commit/i.test(output)) {
  console.log('auto-git-commit: no changes to commit');
  process.exit(0);
}

const refreshedStatus = run(['status', '--short']);
const statusText = refreshedStatus.status === 0 ? refreshedStatus.stdout.trim() : outputOf(refreshedStatus);
requestInput(
  'git commit failed; no build commit was made',
  `Git output:\n${outputOf(commit) || '(no output)'}\n\nCurrent git status:\n${statusText || '(clean)'}`,
  [
    'Fix the Git problem shown above, then rerun the build script.',
    'Commit manually if the automatic build commit should be skipped.',
  ],
);

fail(commit, 'commit changes');
