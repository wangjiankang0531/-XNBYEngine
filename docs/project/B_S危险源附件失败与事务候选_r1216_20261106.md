# 危险源附件保存失败诊断及事务候选 R1216

原完整安全前端、原MyForm/上传控件、原鉴权和原后端在G合成环境普通E实际执行：新建一条自有危险源、一次PNG上传并形成一条base_file关联，实际GET读回上传字节。编辑页只在草稿移除附件，再填原status varchar(4)不能容纳的12345并提交一次，原HTTP500/code100000失败；候选前端保留输入，但原后台先删附件、后更新主记录且无事务，导致主记录仍旧值、关联已丢。原详情和独立只读SQL一致，正常六组件重启后仍丢；7主阶段+2重启阶段，不是9个独立模块，也不算修复验收。源文件对象在首轮失败后仍可读取，重启轮只核主记录/关联，没有声称重启后再次读对象。

所有先存危险源/base_file行排除精确本轮唯一新记录的整表摘要保持；旧任务b876回调、身份角色授权字典保持，11元数据仅verify。两轮控制器/六组件/Chrome正常0、真实Job后代零，1422原代码路径和私有配置/安装/current-run恢复，不改正式18891/8767、用户预览或公网。私有flight在work/hazard-attachment-probe-r1207/hazard-attachment-flight.private.json，mainflow-completed；禁止重建或重放upload/create/failedEdit。本条保留活记录及原上传对象供后续修正，数据未回滚。

最小服务候选只改save(HazardSourceVo)与updateById(HazardSourceVo)：两者rollbackFor=Exception事务，主表insert/update精确成功才处理关联；update false在所有附件写前返回。Reference/原Jar/表结构/权限不变。候选Java17 class已与原归档依赖编译，两个运行时事务注解核实。首次javap用UTF8读取Windows默认编码失败，编译本身已成功；仅显式Java UTF8重新读取，未重复编译或写业务。该候选尚未通过实际Spring生命周期、附件插入阶段故障回滚及通知提交后语义验收，不能把编译或源码注解当运行验收。

R1212首次尝试在CodeTransaction构造时因3条mutation漏source字段拒绝，备份/代码/服务/UI前退出，仅产生marker；真实Job零且每条原代码pin同。保失败，R1214仅补显式source，class/前端相同。固定续接同一个新记录、零create、最多一次新PNG/修复编辑/保附件失败/正常纠正重试/移除草稿失败。R1215在9GiB物理及commit门槛拒绝，未分配父进程、工作目录或parent log，不能说候选运行失败或已启动等待。不得降低门槛或关另一会话窗口；资源满足后核scope/源/current基线，再执行同一尚未分配的R1215入口，不重跑旧R1207/R1213。

按清理SKILL鲜查CIM零、正常终态和恢复SHA后，仅清本轮已恢复before、自有class检查与Chrome profiles，保全部业务/上传/GIS/DLG、最新候选class/dist/计划/私有flight/所有源码和失败日志。原attempt因此不可直接重放。服务器FTP/IIS未清：尚无正常后台通道，固定R48第三稿未上传；沿六任务整项集中发布，不为这份诊断单独更新公网。

D97草稿恢复交接ZIP/SHA/CRC与8个实际Git Blob已独立核收；未重跑其浏览器/RTX测试、未安装合成登录/预览、不代表正常账号宿主/V2/IIS/TLS252接通。源码回执是共享文件，不是实时消息。SSH最后用户前台22222可监听，服务1067原因待服务文件日志，认证仍未过；未再次尝试公网主机或重复安装/启动脚本。完整UE引擎/驻马店GIS DLG多源空间标牌/Web3D及单系统南港目标继续。
