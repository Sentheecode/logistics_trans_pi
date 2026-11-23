"""Exercise actual Qt event loop and widgets against a live collector."""
import os
import pathlib
import subprocess
import sys
import tempfile

collector, hmi = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix="cpp-hmi-") as tmp:
    d = pathlib.Path(tmp) / "data"
    p = subprocess.Popen([collector, str(d), "100", "300"], stdout=subprocess.DEVNULL)
    try:
        result = subprocess.run([hmi, str(d), "6500"], capture_output=True, text=True,
                                timeout=15, env={**os.environ, "QT_QPA_PLATFORM": "offscreen"})
        assert result.returncode == 0, (result.stdout, result.stderr)
        assert "alarm=active" in result.stdout, result.stdout
        assert "quality=bad" in result.stdout and "link=down" in result.stdout, result.stdout
        assert "quality=good link=up" in result.stdout.split("link=down")[-1], result.stdout
        assert p.poll() is None, "HMI exit must not stop collector"
        print(result.stdout)
    finally:
        p.terminate()
        p.wait(timeout=5)
print("PASS: Qt rendered live updates, alarm, bad quality, disconnect/recovery; independent exit")
