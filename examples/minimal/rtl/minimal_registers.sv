`default_nettype none
module minimal_registers ();

  // RMW:BEGIN schema=1
  // This region is synchronized by Register Map Workbench.
  // Edit RMW:OBJECT JSON for text/enumerated properties and RMW:VALUE literals for numeric properties.
  // RMW:OBJECT {"id":"block-control","kind":"block","properties":{"base":"0xF000","description":"Control and status registers.","name":"Control","order":"0","parent":"space-main","size":"0x1000"}}
  localparam logic [63:0] RMW_BLOCK_CONTROL_05569506_BASE = 64'hF000; // RMW:VALUE {"id":"block-control","property":"base"}
  localparam logic [63:0] RMW_BLOCK_CONTROL_05569506_SIZE = 64'h1000; // RMW:VALUE {"id":"block-control","property":"size"}

  // RMW:OBJECT {"id":"field-73f4f13f70d0fa2a","kind":"field","properties":{"description":"","hw_access":"none","lsb":"0","maximum":"","minimum":"","msb":"0","name":"NEW_FIELD","order":"0","parent":"reg-e56277710635ee09","read_side_effect":"none","reset":"0x0","sw_access":"rw","type":"bits","write_side_effect":"write"}}
  localparam int unsigned RMW_FIELD_73F4F13F70D0FA2A_5D741C5D_LSB = 0; // RMW:VALUE {"id":"field-73f4f13f70d0fa2a","property":"lsb"}
  localparam int unsigned RMW_FIELD_73F4F13F70D0FA2A_5D741C5D_MSB = 0; // RMW:VALUE {"id":"field-73f4f13f70d0fa2a","property":"msb"}
  localparam logic [0:0] RMW_FIELD_73F4F13F70D0FA2A_5D741C5D_RESET = 1'h0; // RMW:VALUE {"id":"field-73f4f13f70d0fa2a","property":"reset"}

  // RMW:OBJECT {"id":"field-9f5da9c8400451c0","kind":"field","properties":{"description":"","hw_access":"none","lsb":"0","maximum":"","minimum":"","msb":"0","name":"NEW_FIELD","order":"2","parent":"reg-irq-status","read_side_effect":"none","reset":"0x0","sw_access":"rw","type":"bits","write_side_effect":"write"}}
  localparam int unsigned RMW_FIELD_9F5DA9C8400451C0_15510640_LSB = 0; // RMW:VALUE {"id":"field-9f5da9c8400451c0","property":"lsb"}
  localparam int unsigned RMW_FIELD_9F5DA9C8400451C0_15510640_MSB = 0; // RMW:VALUE {"id":"field-9f5da9c8400451c0","property":"msb"}
  localparam logic [0:0] RMW_FIELD_9F5DA9C8400451C0_15510640_RESET = 1'h0; // RMW:VALUE {"id":"field-9f5da9c8400451c0","property":"reset"}

  // RMW:OBJECT {"id":"field-enable","kind":"field","properties":{"description":"Enables the block.","hw_access":"ro","lsb":"28","maximum":"","minimum":"","msb":"28","name":"sss","order":"0","parent":"reg-control","read_side_effect":"none","reset":"0x0","sw_access":"rw","type":"bool","write_side_effect":"write"}}
  localparam int unsigned RMW_FIELD_ENABLE_E59E1D9D_LSB = 28; // RMW:VALUE {"id":"field-enable","property":"lsb"}
  localparam int unsigned RMW_FIELD_ENABLE_E59E1D9D_MSB = 28; // RMW:VALUE {"id":"field-enable","property":"msb"}
  localparam logic [0:0] RMW_FIELD_ENABLE_E59E1D9D_RESET = 1'h0; // RMW:VALUE {"id":"field-enable","property":"reset"}

  // RMW:OBJECT {"id":"field-error","kind":"field","properties":{"description":"Hardware error indicator.","hw_access":"wo","lsb":"1","maximum":"","minimum":"","msb":"1","name":"ERROR","order":"1","parent":"reg-status","read_side_effect":"none","reset":"0x0","sw_access":"ro","type":"bool","write_side_effect":"none"}}
  localparam int unsigned RMW_FIELD_ERROR_28D038A8_LSB = 1; // RMW:VALUE {"id":"field-error","property":"lsb"}
  localparam int unsigned RMW_FIELD_ERROR_28D038A8_MSB = 1; // RMW:VALUE {"id":"field-error","property":"msb"}
  localparam logic [0:0] RMW_FIELD_ERROR_28D038A8_RESET = 1'h0; // RMW:VALUE {"id":"field-error","property":"reset"}

  // RMW:OBJECT {"id":"field-fc9c50920cb6a8a5","kind":"field","properties":{"description":"","hw_access":"none","lsb":"20","maximum":"","minimum":"","msb":"23","name":"NEW_FIELD","order":"1","parent":"reg-control","read_side_effect":"none","reset":"0x0","sw_access":"rw","type":"bits","write_side_effect":"write"}}
  localparam int unsigned RMW_FIELD_FC9C50920CB6A8A5_42EB2931_LSB = 20; // RMW:VALUE {"id":"field-fc9c50920cb6a8a5","property":"lsb"}
  localparam int unsigned RMW_FIELD_FC9C50920CB6A8A5_42EB2931_MSB = 23; // RMW:VALUE {"id":"field-fc9c50920cb6a8a5","property":"msb"}
  localparam logic [3:0] RMW_FIELD_FC9C50920CB6A8A5_42EB2931_RESET = 4'h0; // RMW:VALUE {"id":"field-fc9c50920cb6a8a5","property":"reset"}

  // RMW:OBJECT {"id":"field-irq-reserved","kind":"field","properties":{"description":"Reserved; keep zero.","hw_access":"none","lsb":"27","maximum":"","minimum":"","msb":"31","name":"RESERVED","order":"1","parent":"reg-irq-status","read_side_effect":"none","reset":"0x0","sw_access":"none","type":"reserved","write_side_effect":"none"}}
  localparam int unsigned RMW_FIELD_IRQ_RESERVED_D1960003_LSB = 27; // RMW:VALUE {"id":"field-irq-reserved","property":"lsb"}
  localparam int unsigned RMW_FIELD_IRQ_RESERVED_D1960003_MSB = 31; // RMW:VALUE {"id":"field-irq-reserved","property":"msb"}
  localparam logic [4:0] RMW_FIELD_IRQ_RESERVED_D1960003_RESET = 5'h0; // RMW:VALUE {"id":"field-irq-reserved","property":"reset"}

  // RMW:OBJECT {"id":"field-pending","kind":"field","properties":{"description":"Pending interrupt sources.","hw_access":"wo","lsb":"12","maximum":"","minimum":"","msb":"15","name":"PENDING","order":"0","parent":"reg-irq-status","read_side_effect":"none","reset":"0x0","sw_access":"rw","type":"bits","write_side_effect":"w1c"}}
  localparam int unsigned RMW_FIELD_PENDING_5DE5B1E1_LSB = 12; // RMW:VALUE {"id":"field-pending","property":"lsb"}
  localparam int unsigned RMW_FIELD_PENDING_5DE5B1E1_MSB = 15; // RMW:VALUE {"id":"field-pending","property":"msb"}
  localparam logic [3:0] RMW_FIELD_PENDING_5DE5B1E1_RESET = 4'h0; // RMW:VALUE {"id":"field-pending","property":"reset"}

  // RMW:OBJECT {"id":"field-ready","kind":"field","properties":{"description":"Hardware is ready.","hw_access":"wo","lsb":"0","maximum":"","minimum":"","msb":"0","name":"READY","order":"0","parent":"reg-status","read_side_effect":"none","reset":"0x1","sw_access":"ro","type":"bool","write_side_effect":"none"}}
  localparam int unsigned RMW_FIELD_READY_9B57E91D_LSB = 0; // RMW:VALUE {"id":"field-ready","property":"lsb"}
  localparam int unsigned RMW_FIELD_READY_9B57E91D_MSB = 0; // RMW:VALUE {"id":"field-ready","property":"msb"}
  localparam logic [0:0] RMW_FIELD_READY_9B57E91D_RESET = 1'h1; // RMW:VALUE {"id":"field-ready","property":"reset"}

  // RMW:OBJECT {"id":"minimal-example","kind":"workspace","properties":{"name":"Minimal Example"}}

  // RMW:OBJECT {"id":"reg-5e1369c084a3f835","kind":"register","properties":{"access":"rw","array_count":"1","description":"","initial":"","maximum":"545","minimum":"1","name":"tt","offset":"0x10","order":"4","parent":"block-control","reserved":"false","reset":"0x0","stride":"0x4","tags":"[]","type":"unsigned","width":"32"}}
  localparam int unsigned RMW_REG_5E1369C084A3F835_FE92F43E_ARRAY_COUNT = 1; // RMW:VALUE {"id":"reg-5e1369c084a3f835","property":"array_count"}
  localparam logic [63:0] RMW_REG_5E1369C084A3F835_FE92F43E_OFFSET = 64'h10; // RMW:VALUE {"id":"reg-5e1369c084a3f835","property":"offset"}
  localparam logic [31:0] RMW_REG_5E1369C084A3F835_FE92F43E_RESET = 32'h0; // RMW:VALUE {"id":"reg-5e1369c084a3f835","property":"reset"}
  localparam logic [63:0] RMW_REG_5E1369C084A3F835_FE92F43E_STRIDE = 64'h4; // RMW:VALUE {"id":"reg-5e1369c084a3f835","property":"stride"}
  localparam int unsigned RMW_REG_5E1369C084A3F835_FE92F43E_WIDTH = 32; // RMW:VALUE {"id":"reg-5e1369c084a3f835","property":"width"}

  // RMW:OBJECT {"id":"reg-67fd5a7da60e1d15","kind":"register","properties":{"access":"rw","array_count":"1","description":"","initial":"","maximum":"","minimum":"","name":"ddd","offset":"0x14","order":"5","parent":"block-control","reserved":"false","reset":"0x0","stride":"0x4","tags":"[]","type":"unsigned","width":"32"}}
  localparam int unsigned RMW_REG_67FD5A7DA60E1D15_9450523B_ARRAY_COUNT = 1; // RMW:VALUE {"id":"reg-67fd5a7da60e1d15","property":"array_count"}
  localparam logic [63:0] RMW_REG_67FD5A7DA60E1D15_9450523B_OFFSET = 64'h14; // RMW:VALUE {"id":"reg-67fd5a7da60e1d15","property":"offset"}
  localparam logic [31:0] RMW_REG_67FD5A7DA60E1D15_9450523B_RESET = 32'h0; // RMW:VALUE {"id":"reg-67fd5a7da60e1d15","property":"reset"}
  localparam logic [63:0] RMW_REG_67FD5A7DA60E1D15_9450523B_STRIDE = 64'h4; // RMW:VALUE {"id":"reg-67fd5a7da60e1d15","property":"stride"}
  localparam int unsigned RMW_REG_67FD5A7DA60E1D15_9450523B_WIDTH = 32; // RMW:VALUE {"id":"reg-67fd5a7da60e1d15","property":"width"}

  // RMW:OBJECT {"id":"reg-control","kind":"register","properties":{"access":"rw","array_count":"1","description":"Global enable and operating mode.","initial":"","maximum":"","minimum":"","name":"CONTROL","offset":"0x0","order":"0","parent":"block-control","reserved":"false","reset":"0x0","stride":"0x4","tags":"[\"test\"]","type":"field","width":"32"}}
  localparam int unsigned RMW_REG_CONTROL_19DB8E3D_ARRAY_COUNT = 1; // RMW:VALUE {"id":"reg-control","property":"array_count"}
  localparam logic [63:0] RMW_REG_CONTROL_19DB8E3D_OFFSET = 64'h0; // RMW:VALUE {"id":"reg-control","property":"offset"}
  localparam logic [31:0] RMW_REG_CONTROL_19DB8E3D_RESET = 32'h0; // RMW:VALUE {"id":"reg-control","property":"reset"}
  localparam logic [63:0] RMW_REG_CONTROL_19DB8E3D_STRIDE = 64'h4; // RMW:VALUE {"id":"reg-control","property":"stride"}
  localparam int unsigned RMW_REG_CONTROL_19DB8E3D_WIDTH = 32; // RMW:VALUE {"id":"reg-control","property":"width"}

  // RMW:OBJECT {"id":"reg-e56277710635ee09","kind":"register","properties":{"access":"rw","array_count":"1","description":"","initial":"","maximum":"","minimum":"","name":"NEW_REGISTER","offset":"0x18","order":"6","parent":"block-control","reserved":"false","reset":"0x0","stride":"0x4","tags":"[]","type":"field","width":"32"}}
  localparam int unsigned RMW_REG_E56277710635EE09_6124215B_ARRAY_COUNT = 1; // RMW:VALUE {"id":"reg-e56277710635ee09","property":"array_count"}
  localparam logic [63:0] RMW_REG_E56277710635EE09_6124215B_OFFSET = 64'h18; // RMW:VALUE {"id":"reg-e56277710635ee09","property":"offset"}
  localparam logic [31:0] RMW_REG_E56277710635EE09_6124215B_RESET = 32'h0; // RMW:VALUE {"id":"reg-e56277710635ee09","property":"reset"}
  localparam logic [63:0] RMW_REG_E56277710635EE09_6124215B_STRIDE = 64'h4; // RMW:VALUE {"id":"reg-e56277710635ee09","property":"stride"}
  localparam int unsigned RMW_REG_E56277710635EE09_6124215B_WIDTH = 32; // RMW:VALUE {"id":"reg-e56277710635ee09","property":"width"}

  // RMW:OBJECT {"id":"reg-e9acdc7d5c2cb466","kind":"register","properties":{"access":"rw","array_count":"1","description":"测试","initial":"","maximum":"295","minimum":"1","name":"r","offset":"0xC","order":"3","parent":"block-control","reserved":"false","reset":"0x0","stride":"0x4","tags":"[]","type":"unsigned","width":"32"}}
  localparam int unsigned RMW_REG_E9ACDC7D5C2CB466_48FE1EB1_ARRAY_COUNT = 1; // RMW:VALUE {"id":"reg-e9acdc7d5c2cb466","property":"array_count"}
  localparam logic [63:0] RMW_REG_E9ACDC7D5C2CB466_48FE1EB1_OFFSET = 64'hC; // RMW:VALUE {"id":"reg-e9acdc7d5c2cb466","property":"offset"}
  localparam logic [31:0] RMW_REG_E9ACDC7D5C2CB466_48FE1EB1_RESET = 32'h0; // RMW:VALUE {"id":"reg-e9acdc7d5c2cb466","property":"reset"}
  localparam logic [63:0] RMW_REG_E9ACDC7D5C2CB466_48FE1EB1_STRIDE = 64'h4; // RMW:VALUE {"id":"reg-e9acdc7d5c2cb466","property":"stride"}
  localparam int unsigned RMW_REG_E9ACDC7D5C2CB466_48FE1EB1_WIDTH = 32; // RMW:VALUE {"id":"reg-e9acdc7d5c2cb466","property":"width"}

  // RMW:OBJECT {"id":"reg-irq-status","kind":"register","properties":{"access":"rw","array_count":"1","description":"Pending interrupt bits; software clears with one.","initial":"","maximum":"","minimum":"","name":"IRQ_STATUS","offset":"0x8","order":"2","parent":"block-control","reserved":"false","reset":"0x0","stride":"0x4","tags":"[]","type":"field","width":"32"}}
  localparam int unsigned RMW_REG_IRQ_STATUS_06704CD3_ARRAY_COUNT = 1; // RMW:VALUE {"id":"reg-irq-status","property":"array_count"}
  localparam logic [63:0] RMW_REG_IRQ_STATUS_06704CD3_OFFSET = 64'h8; // RMW:VALUE {"id":"reg-irq-status","property":"offset"}
  localparam logic [31:0] RMW_REG_IRQ_STATUS_06704CD3_RESET = 32'h0; // RMW:VALUE {"id":"reg-irq-status","property":"reset"}
  localparam logic [63:0] RMW_REG_IRQ_STATUS_06704CD3_STRIDE = 64'h4; // RMW:VALUE {"id":"reg-irq-status","property":"stride"}
  localparam int unsigned RMW_REG_IRQ_STATUS_06704CD3_WIDTH = 32; // RMW:VALUE {"id":"reg-irq-status","property":"width"}

  // RMW:OBJECT {"id":"reg-status","kind":"register","properties":{"access":"ro","array_count":"1","description":"Current hardware status.","initial":"","maximum":"","minimum":"","name":"STATUS","offset":"0x4","order":"1","parent":"block-control","reserved":"false","reset":"0x1","stride":"0x4","tags":"[\"test\"]","type":"field","width":"32"}}
  localparam int unsigned RMW_REG_STATUS_005BA44A_ARRAY_COUNT = 1; // RMW:VALUE {"id":"reg-status","property":"array_count"}
  localparam logic [63:0] RMW_REG_STATUS_005BA44A_OFFSET = 64'h4; // RMW:VALUE {"id":"reg-status","property":"offset"}
  localparam logic [31:0] RMW_REG_STATUS_005BA44A_RESET = 32'h1; // RMW:VALUE {"id":"reg-status","property":"reset"}
  localparam logic [63:0] RMW_REG_STATUS_005BA44A_STRIDE = 64'h4; // RMW:VALUE {"id":"reg-status","property":"stride"}
  localparam int unsigned RMW_REG_STATUS_005BA44A_WIDTH = 32; // RMW:VALUE {"id":"reg-status","property":"width"}

  // RMW:OBJECT {"id":"space-main","kind":"address-space","properties":{"address_width":"32","base":"0x43C00000","description":"","name":"Main","order":"0","parent":"minimal-example"}}
  localparam int unsigned RMW_SPACE_MAIN_B06BDA35_ADDRESS_WIDTH = 32; // RMW:VALUE {"id":"space-main","property":"address_width"}
  localparam logic [63:0] RMW_SPACE_MAIN_B06BDA35_BASE = 64'h43C00000; // RMW:VALUE {"id":"space-main","property":"base"}

  // RMW:END

endmodule
`default_nettype wire
