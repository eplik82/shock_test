# PlatformIO eelskript: pakib portaali lehe (src/web/index.html -> index.html.gz), mtime=0 (korduv tulemus)
import gzip, os
Import("env")  # noqa: F821
src = os.path.join(env.subst("$PROJECT_DIR"), "src", "web", "index.html")  # noqa: F821
dst = src + ".gz"
data = open(src, "rb").read()
gz = gzip.compress(data, 9, mtime=0)
if not os.path.exists(dst) or open(dst, "rb").read() != gz:
    open(dst, "wb").write(gz)
    print("gzip_web: %d -> %d baiti" % (len(data), len(gz)))
