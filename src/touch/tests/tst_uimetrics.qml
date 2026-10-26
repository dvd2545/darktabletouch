import QtQuick
import QtTest
import "../qml"

TestCase {
    name: "UiMetrics"
    UiMetrics { id: metrics }
    function test_physicalTarget() {
        compare(metrics.targetSize(12, 1.5), 76)
        compare(metrics.targetSize(12, 1.75), 65)
        compare(metrics.targetSize(12, 2), 57)
        compare(metrics.targetSize(0, 1), 64)
        compare(metrics.targetSize(1000, 1), 64)
        compare(metrics.targetSize(3, 2), 56)
        compare(metrics.targetSize(20, 1), 80)
    }
}
