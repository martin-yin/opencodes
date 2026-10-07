function uuidToBleBytes(uuidString) {
    // 1. 移除UUID字符串中的所有连字符
    const hexString = uuidString.replace(/-/g, '');
    
    // 2. 检查长度是否为32个字符（128位）
    if (hexString.length !== 32) {
        throw new Error('Invalid UUID format. Must be 32 hex characters after removing hyphens.');
    }
    
    // 3. 将十六进制字符串转换为字节数组（大端序）
    const bytes = [];
    for (let i = 0; i < hexString.length; i += 2) {
        bytes.push(parseInt(hexString.substr(i, 2), 16));
    }
    
    // 4. 将字节数组逆序（转换为小端序）
    bytes.reverse();
    
    // 5. 返回适合BLE_UUID128_INIT的字节数组
    return bytes;
}

// 使用示例


function bleBytesToAndroidUuid(bleBytes) {
    // 1. 检查输入是否为16字节的数组（UUID-128位）
    if (!Array.isArray(bleBytes) || bleBytes.length !== 16) {
        throw new Error('输入必须是包含16个字节的数组');
    }
    
    // 2. 反转字节数组（还原之前的反转操作）
    const reversedBytes = [...bleBytes].reverse();
    
    // 3. 将每个字节转换为两位十六进制字符串
    const hexParts = reversedBytes.map(byte => {
        // 处理可能的负数（转为无符号值）
        const unsignedByte = byte & 0xFF;
        // 转换为十六进制，不足两位补0，保持大写（与UUID常见格式一致）
        return unsignedByte.toString(16).padStart(2, '0');
    });
    
    // 4. 拼接为完整十六进制字符串
    const fullHex = hexParts.join('');
    
    // 5. 按照UUID格式（8-4-4-4-12）添加连字符
    return [
        fullHex.substring(0, 8),
        fullHex.substring(8, 12),
        fullHex.substring(12, 16),
        fullHex.substring(16, 20),
        fullHex.substring(20, 32)
    ].join('-');
}

const hid_control_svc_uuid = [0x46, 0xe0, 0x7d, 0x77, 0xd2, 0xb4, 0xb7, 0x87, 0x81, 0x45, 0x3b, 0x64, 0x4a, 0x2a, 0xed, 0x24];
const hid_command_chr_uuid = [0x34, 0xe5, 0x4a, 0xf2, 0xb2, 0x45, 0x62, 0xb2, 0x40, 0x15, 0xf4, 0xf6, 0x8d, 0x80, 0x40, 0xc3];

console.log("hid_control_svc_uuid:", bleBytesToAndroidUuid(hid_control_svc_uuid)); 
console.log("hid_command_chr_uuid:", bleBytesToAndroidUuid(hid_command_chr_uuid)); 
