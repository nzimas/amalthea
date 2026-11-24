import subprocess,re,sys,bisect
BASE=0xffffff8008080000
syms=[]
for l in open('/k/kallsyms.txt'):
    p=l.split()
    if len(p)>=3 and '[' not in l: syms.append((int(p[0],16),p[2],p[1]))
syms.sort()
addrs=[a for a,_,_ in syms]
byname={n:a for a,n,t in syms if t in 'tT'}
img=open('/k/Image','rb').read()
OBJ={'drivers/usb/gadget/composite.o':['usb_interface_id','config_ep_by_speed','usb_function_activate','usb_add_function','usb_string_ids_n','usb_gstrings_attach','composite_setup','usb_function_deactivate'],
 'drivers/usb/gadget/config.o':['usb_assign_descriptors','usb_free_all_descriptors','usb_copy_descriptors'],
 'drivers/usb/gadget/functions.o':['usb_function_register','usb_function_unregister','usb_get_function_instance','usb_put_function_instance','usb_get_function'],
 'drivers/usb/gadget/epautoconf.o':['usb_ep_autoconfig_ss','usb_ep_autoconfig'],
 'drivers/usb/gadget/udc/core.o':['usb_ep_queue','usb_ep_enable','usb_ep_disable','usb_ep_alloc_request','usb_ep_free_request','usb_ep_dequeue'],
 'sound/core/pcm_lib.o':['snd_pcm_period_elapsed','snd_pcm_lib_ioctl','snd_pcm_hw_constraint_integer','snd_pcm_set_ops'],
 'sound/core/pcm_memory.o':['snd_pcm_lib_malloc_pages','snd_pcm_lib_free_pages','snd_pcm_lib_preallocate_pages_for_all'],
 'sound/core/pcm.o':['snd_pcm_new','_snd_pcm_new'],
 'sound/core/init.o':['snd_card_new','snd_card_register','snd_card_free'],
 'drivers/base/platform.o':['platform_device_register','__platform_driver_register','platform_driver_unregister','platform_device_unregister','platform_device_add'],
 'fs/configfs/item.o':['config_group_init_type_name','config_item_init_type_name']}
BR=re.compile(r'^(b|bl|b\.\w+|cbz|cbnz|tbz|tbnz|adrp|adr)$')
def norm(lines):
    out=[]
    for ins in lines:
        ins=ins.split('//')[0].strip()
        if not ins: continue
        m=ins.split(None,1); op=m[0]; args=m[1] if len(m)>1 else ''
        if BR.match(op):
            args=re.sub(r'(0x)?[0-9a-f]+ <[^>]*>|#?0x[0-9a-f]+|\b[0-9a-f]{6,}\b','T',args)
        out.append(op+' '+args.replace(' ',''))
    return out
def dis_img(name):
    a=byname[name]; i=bisect.bisect_right(addrs,a)
    while addrs[i]==a: i+=1
    n=addrs[i]-a; off=a-BASE
    open('/tmp/f.bin','wb').write(img[off:off+n])
    d=subprocess.run(['objdump','-D','-b','binary','-maarch64','--adjust-vma=%#x'%a,'/tmp/f.bin'],stdout=subprocess.PIPE,universal_newlines=True).stdout
    return [l.split('\t',2)[2] for l in d.splitlines() if re.match(r'^\s*[0-9a-f]+:\t',l) and l.count('\t')>=2]
def dis_obj(o,name):
    d=subprocess.run(['objdump','-d','--no-show-raw-insn','-r','/k/linux-4.9.191/'+o],stdout=subprocess.PIPE,universal_newlines=True).stdout
    m=re.search(r'^[0-9a-f]+ <%s>:\n(.*?)(?:\n\n|\Z)'%re.escape(name),d,re.S|re.M)
    if not m: return None
    res=[];relocated=set()
    for l in m.group(1).splitlines():
        if re.match(r'^\s+[0-9a-f]+: R_',l):
            if res: res[-1]=res[-1]+' RELOC'
            continue
        if re.match(r'^\s*[0-9a-f]+:\t',l): res.append(l.split('\t',1)[1])
    return res
tot=0
import os
if os.environ.get("ONLYDIFF"): OBJ={}
for o,fs in OBJ.items():
    for f in fs:
        if f not in byname: print('%-40s not in kallsyms'%f); continue
        ob=dis_obj(o,f)
        if ob is None: print('%-40s not in object'%f); continue
        im=dis_img(f)
        # strip trailing padding in image
        while im and im[-1].strip() in ('.inst\t0x00000000','udf\t#0','.inst 0x00000000'): im.pop()
        rel=[('RELOC' in x) for x in ob]
        a=norm([x.replace(' RELOC','') for x in ob]); b=norm(im)
        diffs=0
        for i in range(max(len(a),len(b))):
            x=a[i] if i<len(a) else None; y=b[i] if i<len(b) else None
            if x==y: continue
            if i<len(rel) and rel[i]: continue  # relocated operand (address of global)
            diffs+=1
        print('%-40s obj=%4d img=%4d mismatches=%d'%(f,len(a),len(b),diffs))

import difflib
for o,f in [('drivers/usb/gadget/functions.o','usb_put_function_instance'),('drivers/usb/gadget/composite.o','usb_gstrings_attach'),('drivers/usb/gadget/config.o','usb_assign_descriptors'),('drivers/usb/gadget/composite.o','config_ep_by_speed')]:
    a=norm([x.replace(' RELOC','  R') for x in dis_obj(o,f)]); b=norm(dis_img(f))
    print('=====',f)
    for l in difflib.unified_diff(a,b,'obj','img',n=1,lineterm=''): print(l)
