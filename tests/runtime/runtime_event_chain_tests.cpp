#include "kss/runtime_event_chain.hpp"

#include <cassert>

namespace {

using kss::EventRecordStatus;
using kss::ProcessorId;
using kss::RuntimeEventChainRecorder;
using kss::SpcPortDirection;

void record_fixture(RuntimeEventChainRecorder& recorder) {
    assert(recorder.record_cpu_write(
        ProcessorId::snes_cpu, 10, 100, 0x002100, 0x80)
        == EventRecordStatus::accepted);
    assert(recorder.record_spc_port(
        SpcPortDirection::cpu_to_spc, 20, 100, 0, 0xcc)
        == EventRecordStatus::accepted);
    assert(recorder.record_cpu_write(
        ProcessorId::sa1, 7, 101, 0x002200, 0x34)
        == EventRecordStatus::accepted);
    assert(recorder.record_cpu_write(
        ProcessorId::snes_cpu, 11, 102, 0x004300, 0x01)
        == EventRecordStatus::accepted);
    assert(recorder.record_spc_port(
        SpcPortDirection::spc_to_cpu, 21, 102, 0, 0xcc)
        == EventRecordStatus::accepted);
    assert(recorder.record_cpu_write(
        ProcessorId::snes_cpu, 12, 103, 0x00430b, 0x03)
        == EventRecordStatus::accepted);
}

void test_oracle_compatible_chain_digests_and_derived_register_views() {
    RuntimeEventChainRecorder recorder;
    record_fixture(recorder);
    const auto summary = recorder.summary();

    assert(summary.cpu_writes.records == 4);
    assert(summary.cpu_writes.sha256
        == "e92370df0ec717b8bcecda55dccbc5b86a3aac38e9507dc3ecb1456cb2e808b9");
    assert(summary.scpu_writes.records == 3);
    assert(summary.scpu_writes.sha256
        == "87513422519595539e3357cc235ace0e924d51b59a9cabaa546b79dc36e326c1");
    assert(summary.sa1_writes.records == 1);
    assert(summary.sa1_writes.sha256
        == "1c652ce16cc8f6a874a03e651d7df23266782ce0d94bbf6db6da5f3060cd5d26");
    assert(summary.ppu_register_writes.records == 1);
    assert(summary.ppu_register_writes.sha256
        == "a642ba1a7088e4b66f6723694137a65d5c3dddb02a84f97347a495e3cf82d4ec");
    assert(summary.dma_register_writes.records == 2);
    assert(summary.dma_register_writes.sha256
        == "38632be7786efe192dc7a3adb539d774633fe5e6d76ce6f79fd70556fd3b174a");
    assert(summary.spc_ports.records == 2);
    assert(summary.spc_ports.sha256
        == "77df3a5daba936040245b2f18198118172728874093d2f2c46732c215c7ed01b");
    assert(summary.cross_domain_order.records == 6);
    assert(summary.cross_domain_order.sha256
        == "2d068cac56adab5a209f7cd61acd89c0387ef846318074855bbb5bbfce66bacb");
}

void test_monotonic_and_range_failures_are_atomic() {
    RuntimeEventChainRecorder recorder;
    record_fixture(recorder);
    const auto before = recorder.summary();

    assert(recorder.record_cpu_write(
        ProcessorId::snes_cpu, 13, 102, 0, 0)
        == EventRecordStatus::past_master_clock);
    assert(recorder.record_cpu_write(
        ProcessorId::snes_cpu, 11, 104, 0, 0)
        == EventRecordStatus::past_local_cycle);
    assert(recorder.record_cpu_write(
        static_cast<ProcessorId>(2), 13, 104, 0, 0)
        == EventRecordStatus::invalid_processor);
    assert(recorder.record_cpu_write(
        ProcessorId::snes_cpu, 13, 104, 0x01000000, 0)
        == EventRecordStatus::invalid_address);
    assert(recorder.record_spc_port(
        SpcPortDirection::cpu_to_spc, 22, 104, 4, 0)
        == EventRecordStatus::invalid_port);
    assert(recorder.record_spc_port(
        SpcPortDirection::spc_to_cpu, 20, 104, 0, 0)
        == EventRecordStatus::past_local_cycle);
    assert(recorder.summary() == before);
}

void test_comparison_reports_each_independent_chain() {
    RuntimeEventChainRecorder recorder;
    record_fixture(recorder);
    const auto expected = recorder.summary();
    recorder.clear();
    const auto empty = recorder.summary();
    const auto mismatches = kss::compare_event_chain_summaries(expected, empty);

    assert(kss::has_mismatch(mismatches, kss::EventChainMismatch::cpu_writes));
    assert(kss::has_mismatch(mismatches, kss::EventChainMismatch::scpu_writes));
    assert(kss::has_mismatch(mismatches, kss::EventChainMismatch::sa1_writes));
    assert(kss::has_mismatch(mismatches, kss::EventChainMismatch::ppu_register_writes));
    assert(kss::has_mismatch(mismatches, kss::EventChainMismatch::dma_register_writes));
    assert(kss::has_mismatch(mismatches, kss::EventChainMismatch::spc_ports));
    assert(kss::has_mismatch(mismatches, kss::EventChainMismatch::cross_domain_order));
    auto oracle_expected = empty;
    oracle_expected.cross_domain_order = expected.cross_domain_order;
    assert(kss::compare_event_chain_summaries(
        oracle_expected, empty, kss::kOracleEventChainComparisons)
        == kss::EventChainMismatch::none);
    assert(kss::compare_event_chain_summaries(empty, empty)
        == kss::EventChainMismatch::none);
}

} // namespace

int main() {
    test_oracle_compatible_chain_digests_and_derived_register_views();
    test_monotonic_and_range_failures_are_atomic();
    test_comparison_reports_each_independent_chain();
}
