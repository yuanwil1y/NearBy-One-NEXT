import json
from pathlib import Path
import tempfile
import unittest

import bt_numbers_sources


class BluetoothNumbersSourcesTest(unittest.TestCase):
    def test_company_and_service_records_are_static_and_normalized(self):
        with tempfile.TemporaryDirectory() as td_text:
            td = Path(td_text)
            companies = td / "company_ids.json"
            services = td / "service_uuids.json"
            companies.write_text(json.dumps([
                {"code": 13, "name": "Texas Instruments Inc."},
                {"code": -1, "name": "bad"},
            ]))
            services.write_text(json.dumps([
                {"name": "Battery Service", "identifier": "org.bluetooth.service.battery_service", "uuid": "180F", "source": "gss"},
                {"name": "Custom", "uuid": "12345678"},
                {"name": "bad", "uuid": "xyz"},
            ]))
            company_records = bt_numbers_sources.extract_company_ids(companies, "bt-pin")
            service_records = bt_numbers_sources.extract_service_uuids(services, "bt-pin")

        self.assertEqual([r["key"] for r in company_records], ["ble:assigned:company:000d"])
        self.assertEqual(company_records[0]["classification"], "DATA_ONLY")
        self.assertEqual(company_records[0]["source_id"], "bluetooth_numbers_nordic")
        self.assertEqual(service_records[0]["uuid"], "0000180f-0000-1000-8000-00805f9b34fb")
        self.assertEqual(service_records[1]["uuid"], "12345678-0000-1000-8000-00805f9b34fb")
        self.assertEqual(len(service_records), 2)

    def test_uuid_rejects_non_assigned_shapes(self):
        with self.assertRaises(ValueError):
            bt_numbers_sources.canonical_bluetooth_uuid("123")


if __name__ == "__main__":
    unittest.main()
