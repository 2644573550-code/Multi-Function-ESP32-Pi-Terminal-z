// KDF 延迟探针：为 B-06 提供可复现的数字，不写死任何机器假设。
//
// 用法：
//   ./kdf_bench                    # 默认 600000 次迭代，跑 3 轮取中位数
//   ./kdf_bench 600000 5           # 指定迭代数与轮数
//
// 目的：文档 §0.2 预估 Pi 3B 单次 0.5~1.5 秒，超过 2 秒要提 PR 附数据。
// 本机数字不能代入 Pi，但把这个探针一起带到 Pi 上跑就能直接对比。
#include "security/PinPolicy.h"

#include <QElapsedTimer>
#include <QCoreApplication>
#include <QTextStream>
#include <algorithm>
#include <vector>

using namespace security;

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    int iterations = PinPolicy::kProductionIterations;
    int rounds = 3;
    if (argc > 1) iterations = QString::fromLocal8Bit(argv[1]).toInt();
    if (argc > 2) rounds = QString::fromLocal8Bit(argv[2]).toInt();
    if (iterations < 1 || rounds < 1) {
        QTextStream(stderr) << "usage: kdf_bench [iterations] [rounds]\n";
        return 2;
    }

    const QByteArray salt = PinPolicy::salt16();
    const QString pin = QStringLiteral("246810");
    std::vector<double> samples;
    samples.reserve(rounds);

    for (int i = 0; i < rounds; ++i) {
        QElapsedTimer timer;
        timer.start();
        const QByteArray key = PinPolicy::deriveKek(pin, salt, iterations);
        const qint64 elapsed = timer.elapsed();
        if (key.size() != PinPolicy::kKekBytes) {
            QTextStream(stderr) << "deriveKek returned " << key.size() << " bytes, expected "
                                << PinPolicy::kKekBytes << "\n";
            return 3;
        }
        samples.push_back(double(elapsed));
        out << "round " << (i + 1) << ": " << elapsed << " ms\n";
    }

    std::sort(samples.begin(), samples.end());
    const double median = samples[samples.size() / 2];
    out << "iterations=" << iterations
        << " median=" << median << " ms"
        << " min=" << samples.front() << " ms"
        << " max=" << samples.back() << " ms\n";

    // 文档 §0.2 的验收线：超过 2 秒要提 PR 附实测，不得自行降低迭代数。
    out << (median > 2000.0 ? "WARN: exceeds 2000 ms; report to F with this output\n"
                            : "OK: within 2000 ms\n");
    out << "production floor is " << PinPolicy::kProductionIterations << "\n";
    return 0;
}
