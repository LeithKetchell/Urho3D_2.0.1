#!/usr/bin/env python3
# broker_exec.py — send an EXEC request to the Claudette broker and act as a
# transparent proxy: print the command's stdout/stderr and exit with its code.
# The command runs as the local user inside the broker. See
# Claude/CLAUDETTE_WRITE_EXEC_BROKER_PLAN.md (phase 4).
#
#   SOCK=<broker.sock> broker_exec.py <cwd> <arg0> [arg1 ...]
#
# Reply framing from the broker:
#   OK\n<exit>\n<stdout-len>\n<stdout><stderr-len>\n<stderr>   (success path)
#   DENY <reason>\n  /  ERR <reason>\n                         (refused / failed)
import os, sys, socket


def main():
    sock = os.environ.get('SOCK')
    if not sock or len(sys.argv) < 3:
        sys.stderr.write("usage: SOCK=<sock> broker_exec.py <cwd> <arg0> [arg1 ...]\n")
        sys.exit(2)
    cwd = sys.argv[1]
    argv = sys.argv[2:]

    frame = b'EXEC\n' + cwd.encode('utf-8') + b'\n' + str(len(argv)).encode() + b'\n'
    for a in argv:
        ab = a.encode('utf-8')
        frame += str(len(ab)).encode() + b'\n' + ab

    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    # Short timeout for connect so a missing/dead broker fails fast...
    s.settimeout(15)
    try:
        s.connect(sock)
    except Exception as e:
        sys.stderr.write("broker connect failed: %s\n" % e)
        sys.exit(1)
    # ...then widen for the reply: the broker buffers stdout/stderr and only
    # answers once the command finishes, and an EXEC may be a multi-minute build.
    # Sit just past the broker's own 1800s EXEC cap so its timeout fires first.
    s.settimeout(1850)
    try:
        s.sendall(frame)
        s.shutdown(socket.SHUT_WR)
    except Exception:
        pass
    data = b''
    try:
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            data += chunk
    except Exception:
        pass
    s.close()

    if data.startswith(b'DENY') or data.startswith(b'ERR'):
        sys.stderr.write(data.decode('utf-8', 'replace'))
        sys.exit(3)

    # Parse OK\n<exit>\n<outlen>\n<out><errlen>\n<err> (out/err are binary, length-prefixed).
    def readline(d, i):
        j = d.index(b'\n', i)
        return d[i:j], j + 1
    try:
        _tag, i = readline(data, 0)            # "OK"
        exitb, i = readline(data, i)
        outlenb, i = readline(data, i)
        outlen = int(outlenb)
        out = data[i:i + outlen]; i += outlen
        errlenb, i = readline(data, i)
        errlen = int(errlenb)
        err = data[i:i + errlen]; i += errlen
    except Exception as e:
        sys.stderr.write("broker: malformed reply %r (%s)\n" % (data[:80], e))
        sys.exit(4)

    sys.stdout.buffer.write(out); sys.stdout.buffer.flush()
    sys.stderr.buffer.write(err); sys.stderr.buffer.flush()
    sys.exit(int(exitb))


if __name__ == '__main__':
    main()
