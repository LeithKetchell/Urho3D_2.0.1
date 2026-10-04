#!/usr/bin/env python3
# broker_write.py — content/transport half of the broker_write.sh PreToolUse hook.
# Kept separate so it can be unit-tested directly with SOCK pointed at a live broker.
# See Claude/CLAUDETTE_WRITE_EXEC_BROKER_PLAN.md (Layer 2 / phase 2).
#
# Reads the PreToolUse JSON from $INPUT_FILE, computes the FULL new file content
# (Write -> content; Edit -> current file with old_string->new_string applied),
# sends a WRITE frame to the broker at $SOCK, and signals the wrapper via exit code:
#   exit 0  -> fall open (not ours / can't apply) — let claude's own tool run
#   exit 2  -> block claude's tool; stdout carries the message for the model
import os, sys, json, socket


def fall_open():
    sys.exit(0)


def block(msg):
    sys.stdout.write(msg)
    sys.exit(2)


def main():
    try:
        data = json.loads(open(os.environ['INPUT_FILE'], 'rb').read())
    except Exception:
        fall_open()

    tool = data.get('tool_name', '')
    ti = data.get('tool_input', {}) or {}
    fp = ti.get('file_path', '')
    root = os.path.realpath(os.environ['PROJECT_ROOT'])

    if tool not in ('Write', 'Edit') or not fp:
        fall_open()

    # Absolutize lexically — the target may not exist yet; the broker canonicalizes
    # the parent dir itself.
    if not os.path.isabs(fp):
        fp = os.path.join(root, fp)
    fp = os.path.abspath(fp)

    # Brokerable scope only: inside the tree, never the cage. Mirrors the C++
    # BrokerPolicyCheckPath. Anything else -> claude handles it normally for now.
    rel = os.path.relpath(fp, root)
    if rel == '..' or rel.startswith('../'):
        fall_open()
    if rel == 'CLAUDE.md' or rel.startswith('.claude/') or rel.startswith('.git/'):
        fall_open()

    if tool == 'Write':
        newbytes = (ti.get('content', '') or '').encode('utf-8')
    else:  # Edit -> collapse to a whole-file WRITE
        old = ti.get('old_string', '')
        new = ti.get('new_string', '')
        replace_all = bool(ti.get('replace_all', False))
        try:
            cur = open(fp, 'r', encoding='utf-8').read()
        except Exception:
            fall_open()          # can't read -> let claude's Edit raise its own error
        if old and old not in cur:
            fall_open()          # stale old_string -> let claude's Edit report it
        updated = cur.replace(old, new) if replace_all else cur.replace(old, new, 1)
        newbytes = updated.encode('utf-8')

    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(5)
    try:
        s.connect(os.environ['SOCK'])
    except Exception:
        fall_open()              # broker truly unreachable -> transition-safe

    # The broker may deny at the peer-credential stage and close BEFORE reading the
    # frame, so sendall() can raise BrokenPipeError with the DENY already waiting on
    # the wire. Don't let a send error mask the reply — always try to read it.
    frame = ('WRITE\n%s\n%d\n' % (fp, len(newbytes))).encode('utf-8') + newbytes
    try:
        s.sendall(frame)
        s.shutdown(socket.SHUT_WR)
    except Exception:
        pass
    reply = b''
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            reply += chunk
    except Exception:
        pass
    s.close()

    reply = reply.decode('utf-8', 'replace').strip()
    if reply == 'OK':
        block("Claudette wrote %s as the local user via the broker. The file is ALREADY "
              "written to disk — do not retry this Write/Edit (it was intentionally "
              "suppressed so the file is owned by the local user, not claude)." % rel)
    elif reply.startswith('DENY') or reply.startswith('ERR'):
        block("Broker refused to write %s: '%s'. The file was NOT written. Do not "
              "retry as a direct write — fix the path/policy or ask the local user." % (rel, reply))
    else:
        # No usable reply (couldn't send AND nothing came back) -> the channel died
        # with nothing said. Fall open so claude isn't bricked mid-transition.
        fall_open()


if __name__ == '__main__':
    main()
