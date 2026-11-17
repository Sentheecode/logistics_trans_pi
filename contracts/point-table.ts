/** 用户确认的点表结构：code 在 name 前，同一行可含读、写两侧。尚非可执行配置。 */
export interface PointSide {
  source_row_id: string | null;
  name: string; // 两侧来源名称可能不同，例如机器人心跳 / PC心跳
  db_number: number;
  offset: string; // 如 "0.1"、"10.0"；绝不能解析成浮点数
  data_type: string; // 保留来源 BOOL / int / INT / string，协议类型另行映射
  length: number; // 来源长度，不擅自解释成字节数
  reserved: boolean;
  source_direction: string | null;
}
interface PointRowBase {
  id: string; // 内部主键，与现场 code 分开
  code: string | null; // 未分配时为 null；按文本保存，保留前导零
  name: string;
  plc_id: string;
  station_code: string | null;
  pairing_status: 'single_sided' | 'candidate' | 'confirmed';
  // candidate 只是读写同功能候选，不证明运行握手/OPC UA 映射已确认。
}
export type PointTableRow = PointRowBase & (
  | { read: PointSide; write: PointSide | null }
  | { read: null; write: PointSide }
);
// read/write 是表内两组列的类型表达，不要求数据库使用 JSON 存储。
// code 未分配、配对仅候选、预留侧或映射未审核时，不能据此生成有效写权限。
