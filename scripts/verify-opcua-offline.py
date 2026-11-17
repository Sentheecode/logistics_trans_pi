#!/usr/bin/env python3
"""对本机已安装驱动依赖建立离线镜像并还原验证；不下载或安装任何包。"""
import hashlib,json,os,pathlib,platform,shutil,subprocess,tarfile,tempfile
root=pathlib.Path(__file__).resolve().parents[1]
driver=root/'runtime/drivers/opcua'
artifacts=root/'t5/artifacts';artifacts.mkdir(parents=True,exist_ok=True)
archive=artifacts/'opcua-linux-node_modules.tar.gz'
def inventory(base):
    result={}
    for path in sorted(base.rglob('*')):
        key=str(path.relative_to(base))
        if path.is_symlink():result[key]={'symlink':os.readlink(path)}
        elif path.is_file():result[key]={'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'mode':path.stat().st_mode&0o777}
    return result
original=inventory(driver/'node_modules')
with tarfile.open(archive,'w:gz') as tar:
    for name in ['node_modules','package.json','package-lock.json']:tar.add(driver/name,arcname=name)
with tempfile.TemporaryDirectory(prefix='ltp-opcua-offline-') as folder:
    restored=pathlib.Path(folder)
    with tarfile.open(archive) as tar:tar.extractall(restored,filter='data')
    actual=inventory(restored/'node_modules')
    if original!=actual:raise RuntimeError('还原文件/链接/权限与镜像不一致')
    for name in ['src','test']:shutil.copytree(driver/name,restored/name)
    # node --test直接执行，仅连接测试自己创建的本地UA服务端；无npm、无安装命令。
    run=subprocess.run(['node','--test',*[str(p) for p in sorted((restored/'test').glob('*.test.mjs'))]],cwd=restored,text=True,capture_output=True)
    (root/'t5/evidence/offline-driver-tests.txt').write_text(run.stdout+run.stderr)
    if run.returncode:raise RuntimeError('还原后的驱动测试失败，见offline-driver-tests.txt')
report={'platform':platform.platform(),'node':subprocess.check_output(['node','--version'],text=True).strip(),
 'archive_relative_path':str(archive.relative_to(root)),'archive_bytes':archive.stat().st_size,
 'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'installed_file_or_link_count':len(original),
 'restored_file_hashes_links_and_modes_equal':True,'restored_driver_tests_exit_code':run.returncode,
 'package_install_or_fetch_commands_used_during_restore':False,
 'limitation':'本机Linux/Node24的依赖快照；不证明Windows、Node22最低版本或现场离线安装已通过。归档未上传GitHub。'}
(root/'t5/evidence/offline-restore.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
print(json.dumps(report,ensure_ascii=False,indent=2))
