#include "security/HostIdentity.h"

#include "security/TrustStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <mbedtls/entropy.h>
#include <mbedtls/platform_util.h>

#include <cstring>

// 只用 mbedtls 2.28 与 3.6 的**公共** ecp/ecdsa API，不碰 `mbedtls_ecdsa_context`
// 的字段：2.28 是裸 `grp/d/Q`，3.6 变成 `MBEDTLS_PRIVATE(grp)`，直接访问会在
// 某一侧编译失败（F 已踩过同类坑，见 pbr-secure-v1 §8）。
//
// 私钥文件格式（自定义 PEM 包装）：
//   -----BEGIN PBR HOST IDENTITY-----
//   <base64，32 字节大端标量>
//   -----END PBR HOST IDENTITY-----
// 为什么不自造 PKCS#8：`mbedtls_pk_*` 在 2.28/3.x 差异大，而这个文件只有本
// 程序读写，P0 已明确「私钥明文、仅测试级」。用固定长度标量可避免任何版本
// 相关的解析分支。

namespace {
constexpr char kPemBegin[] = "-----BEGIN PBR HOST IDENTITY-----";
constexpr char kPemEnd[] = "-----END PBR HOST IDENTITY-----";
constexpr auto kFileMode = QFile::ReadOwner | QFile::WriteOwner;

QString defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
         + QStringLiteral("/host_identity.pem");
}

void setError(QString *error, const QString &text)
{
    if (error) *error = text;
}

// RAII：RNG
struct Rng
{
    mbedtls_entropy_context entropy{};
    mbedtls_ctr_drbg_context ctr{};
    bool ready = false;

    Rng()
    {
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&ctr);
        static const unsigned char personalization[] = "pbr-host-identity";
        ready = mbedtls_ctr_drbg_seed(&ctr, mbedtls_entropy_func, &entropy,
                                      personalization, sizeof(personalization) - 1) == 0;
    }
    ~Rng()
    {
        mbedtls_ctr_drbg_free(&ctr);
        mbedtls_entropy_free(&entropy);
    }
};

QByteArray pemWrap(const QByteArray &raw)
{
    const QByteArray b64 = raw.toBase64();
    QByteArray out;
    out += kPemBegin;
    out += '\n';
    for (int i = 0; i < b64.size(); i += 64) out += b64.mid(i, 64) + '\n';
    out += kPemEnd;
    out += '\n';
    return out;
}

bool pemUnwrap(const QByteArray &text, QByteArray *raw)
{
    const int begin = text.indexOf(kPemBegin);
    const int end = text.indexOf(kPemEnd);
    if (begin < 0 || end < 0 || end <= begin) return false;
    QByteArray body = text.mid(begin + int(sizeof(kPemBegin) - 1), end - begin - int(sizeof(kPemBegin) - 1));
    body.replace('\n', "").replace('\r', "").replace(' ', "");
    *raw = QByteArray::fromBase64Encoding(body, QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors).decoded;
    return !raw->isEmpty();
}
} // namespace

struct HostIdentity::Impl
{
    mbedtls_ecp_group grp{};
    mbedtls_mpi d{};
    mbedtls_ecp_point Q{};
    bool haveKey = false;

    Impl()
    {
        mbedtls_ecp_group_init(&grp);
        mbedtls_mpi_init(&d);
        mbedtls_ecp_point_init(&Q);
    }
    ~Impl() { clear(); }
    void clear()
    {
        haveKey = false;
        mbedtls_mpi_free(&d);
        mbedtls_mpi_init(&d);
        mbedtls_ecp_point_free(&Q);
        mbedtls_ecp_point_init(&Q);
        // 曲线参数不含秘密，但一并清掉更省心。
        mbedtls_ecp_group_free(&grp);
        mbedtls_ecp_group_init(&grp);
    }
};

HostIdentity::HostIdentity(QString path)
    : d_(std::make_unique<Impl>())
{
    path_ = path.isEmpty() ? defaultPath() : std::move(path);
}

HostIdentity::~HostIdentity() = default;

QString HostIdentity::path() const
{
    return path_;
}

bool HostIdentity::exists() const
{
    return QFile::exists(path_);
}

bool HostIdentity::hasKey() const
{
    return d_ && d_->haveKey;
}

void HostIdentity::reset()
{
    if (d_) d_->clear();
}

bool HostIdentity::generate(QString *error, bool overwrite)
{
    if (exists() && !overwrite) {
        setError(error, QStringLiteral("host identity already exists; pass overwrite to replace it"));
        return false;
    }

    Rng rng;
    if (!rng.ready) {
        setError(error, QStringLiteral("cannot seed CSPRNG"));
        return false;
    }

    // 先在临时 EC 结构里生成，只把 32 字节标量落盘；成功后统一走 load()
    // 重建内存态。**不要**拷贝 Impl：mbedtls 的 group/mpi/point 是浅结构，
    // 按值复制会双重释放。
    mbedtls_ecp_group grp;
    mbedtls_mpi d;
    mbedtls_ecp_point Q;
    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&d);
    mbedtls_ecp_point_init(&Q);

    QByteArray seed(32, '\0');
    bool ok = false;
    do {
        if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) != 0) {
            setError(error, QStringLiteral("cannot load P-256 group"));
            break;
        }
        if (mbedtls_ecp_gen_privkey(&grp, &d, mbedtls_ctr_drbg_random, &rng.ctr) != 0) {
            setError(error, QStringLiteral("cannot generate private key"));
            break;
        }
        // 3.x 对 f_rng = NULL 直接报错（F 记录的第 5 个陷阱），两版都传 CSPRNG。
        if (mbedtls_ecp_mul(&grp, &Q, &d, &grp.G, mbedtls_ctr_drbg_random, &rng.ctr) != 0) {
            setError(error, QStringLiteral("cannot derive public key"));
            break;
        }
        if (mbedtls_mpi_write_binary(&d, reinterpret_cast<unsigned char *>(seed.data()), 32) != 0) {
            setError(error, QStringLiteral("cannot serialize private key"));
            break;
        }
        ok = true;
    } while (false);

    mbedtls_ecp_group_free(&grp);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_point_free(&Q);
    if (!ok) {
        mbedtls_platform_zeroize(seed.data(), size_t(seed.size()));
        return false;
    }

    const QFileInfo info(path_);
    if (!QDir().mkpath(info.absolutePath())) {
        mbedtls_platform_zeroize(seed.data(), size_t(seed.size()));
        setError(error, QStringLiteral("cannot create identity directory"));
        return false;
    }
    bool written = false;
    {
        QSaveFile file(path_);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(pemWrap(seed));
            written = file.commit();
            if (!written) setError(error, file.errorString());
        } else {
            setError(error, file.errorString());
        }
    }
    mbedtls_platform_zeroize(seed.data(), size_t(seed.size()));
    if (!written) return false;

    QFile::setPermissions(path_, kFileMode);

    d_->clear();
    return load(error);
}

bool HostIdentity::load(QString *error)
{
    QFile file(path_);
    if (!file.exists()) {
        setError(error, QStringLiteral("host identity not found"));
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, file.errorString());
        return false;
    }
    QByteArray raw;
    if (!pemUnwrap(file.readAll(), &raw) || raw.size() != 32) {
        setError(error, QStringLiteral("host identity file is corrupt"));
        return false;
    }

    Rng rng;
    if (!rng.ready) {
        setError(error, QStringLiteral("cannot seed CSPRNG"));
        return false;
    }

    // 直接装载进 d_；失败时清干净，绝不留半初始化状态（避免"有公钥没私钥"）。
    d_->clear();
    int rc = mbedtls_ecp_group_load(&d_->grp, MBEDTLS_ECP_DP_SECP256R1);
    if (rc == 0)
        rc = mbedtls_mpi_read_binary(&d_->d, reinterpret_cast<const unsigned char *>(raw.constData()), 32);
    if (rc == 0)
        rc = mbedtls_ecp_mul(&d_->grp, &d_->Q, &d_->d, &d_->grp.G,
                             mbedtls_ctr_drbg_random, &rng.ctr);
    mbedtls_platform_zeroize(raw.data(), size_t(raw.size()));
    if (rc != 0) {
        d_->clear();
        setError(error, QStringLiteral("host identity scalar is invalid"));
        return false;
    }

    d_->haveKey = true;
    return true;
}

bool HostIdentity::sign(const QByteArray &hash, QByteArray *signature, QString *error) const
{
    if (!signature) {
        setError(error, QStringLiteral("signature output is null"));
        return false;
    }
    signature->clear();
    if (!d_ || !d_->haveKey) {
        setError(error, QStringLiteral("no private key loaded"));
        return false;
    }
    if (hash.size() != kHashBytes) {
        setError(error, QStringLiteral("input must be a 32-byte SHA-256 digest"));
        return false;
    }

    Rng rng;
    if (!rng.ready) {
        setError(error, QStringLiteral("cannot seed CSPRNG"));
        return false;
    }

    mbedtls_mpi r;
    mbedtls_mpi s;
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    const int rc = mbedtls_ecdsa_sign(&d_->grp, &r, &s, &d_->d,
                                      reinterpret_cast<const unsigned char *>(hash.constData()),
                                      size_t(hash.size()), mbedtls_ctr_drbg_random, &rng.ctr);
    bool ok = rc == 0;
    if (ok) {
        QByteArray raw(kSignatureBytes, '\0');
        ok = mbedtls_mpi_write_binary(&r, reinterpret_cast<unsigned char *>(raw.data()), 32) == 0
          && mbedtls_mpi_write_binary(&s, reinterpret_cast<unsigned char *>(raw.data()) + 32, 32) == 0;
        if (ok) *signature = raw;
    }
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    if (!ok) setError(error, QStringLiteral("ECDSA sign failed"));
    return ok;
}

QByteArray HostIdentity::publicKey() const
{
    if (!d_ || !d_->haveKey) return {};
    QByteArray out(kPublicKeyBytes, '\0');
    size_t written = 0;
    if (mbedtls_ecp_point_write_binary(&d_->grp, &d_->Q, MBEDTLS_ECP_PF_UNCOMPRESSED,
                                       &written, reinterpret_cast<unsigned char *>(out.data()),
                                       size_t(out.size())) != 0
        || written != size_t(kPublicKeyBytes)) {
        return {};
    }
    return out;
}

QByteArray HostIdentity::fingerprint() const
{
    return TrustStore::fingerprintOf(publicKey());
}

quint8 HostIdentity::keyId() const
{
    return TrustStore::keyIdOf(publicKey());
}

bool HostIdentity::verify(const QByteArray &publicKey, const QByteArray &hash, const QByteArray &signature)
{
    if (publicKey.size() != kPublicKeyBytes || hash.size() != kHashBytes
        || signature.size() != kSignatureBytes) {
        return false;
    }

    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
    mbedtls_mpi r;
    mbedtls_mpi s;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Q);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    bool ok = false;
    do {
        if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) != 0) break;
        if (mbedtls_ecp_point_read_binary(&grp, &Q,
                reinterpret_cast<const unsigned char *>(publicKey.constData()),
                size_t(publicKey.size())) != 0) break;
        if (mbedtls_ecp_check_pubkey(&grp, &Q) != 0) break;
        if (mbedtls_mpi_read_binary(&r,
                reinterpret_cast<const unsigned char *>(signature.constData()), 32) != 0) break;
        if (mbedtls_mpi_read_binary(&s,
                reinterpret_cast<const unsigned char *>(signature.constData()) + 32, 32) != 0) break;
        ok = mbedtls_ecdsa_verify(&grp,
                reinterpret_cast<const unsigned char *>(hash.constData()), size_t(hash.size()),
                &Q, &r, &s) == 0;
    } while (false);

    mbedtls_ecp_group_free(&grp);
    mbedtls_ecp_point_free(&Q);
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    return ok;
}

bool HostIdentity::derToRaw(const QByteArray &der, QByteArray *raw)
{
    // SEQUENCE { INTEGER r, INTEGER s }，两端各去掉前导 0x00 填充后左补零到 32。
    if (!raw || der.size() < 8 || quint8(der.at(0)) != 0x30) return false;
    int offset = 1;
    auto readLength = [&der, &offset](int *length) -> bool {
        if (offset >= der.size()) return false;
        const quint8 first = quint8(der.at(offset++));
        if (first < 0x80) { *length = first; return true; }
        const int count = first & 0x7f;
        if (count < 1 || count > 2 || offset + count > der.size()) return false;
        int value = 0;
        for (int i = 0; i < count; ++i) value = (value << 8) | quint8(der.at(offset++));
        *length = value;
        return true;
    };
    int seqLength = 0;
    if (!readLength(&seqLength)) return false;
    if (offset + seqLength > der.size()) return false;

    auto readInteger = [&der, &offset](QByteArray *out) -> bool {
        if (offset >= der.size() || quint8(der.at(offset++)) != 0x02) return false;
        if (offset >= der.size()) return false;
        const int length = quint8(der.at(offset++));
        if (length <= 0 || offset + length > der.size()) return false;
        QByteArray value = der.mid(offset, length);
        offset += length;
        while (value.size() > 1 && value.at(0) == '\0') value.remove(0, 1);
        if (value.size() > 32) return false;
        *out = QByteArray(32 - value.size(), '\0') + value;
        return true;
    };
    QByteArray rPart;
    QByteArray sPart;
    if (!readInteger(&rPart) || !readInteger(&sPart)) return false;
    *raw = rPart + sPart;
    return true;
}

bool HostIdentity::rawToDer(const QByteArray &raw, QByteArray *der)
{
    if (!der || raw.size() != kSignatureBytes) return false;
    auto encodeInteger = [](const QByteArray &value) {
        QByteArray body = value;
        while (body.size() > 1 && body.at(0) == '\0') body.remove(0, 1);
        // 最高位为 1 时补 0x00，避免被当作负数。
        if (quint8(body.at(0)) & 0x80) body.prepend('\0');
        QByteArray out;
        out.append(char(0x02));
        out.append(char(body.size()));
        out.append(body);
        return out;
    };
    const QByteArray rEnc = encodeInteger(raw.left(32));
    const QByteArray sEnc = encodeInteger(raw.mid(32, 32));
    const QByteArray body = rEnc + sEnc;
    der->clear();
    der->append(char(0x30));
    der->append(char(body.size()));
    der->append(body);
    return true;
}
