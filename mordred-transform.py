import argparse
import os

class cd:
    """Context manager for changing the current working directory"""
    def __init__(self, newPath):
        self.newPath = os.path.expanduser(newPath)

    def __enter__(self):
        self.savedPath = os.getcwd()
        os.chdir(self.newPath)

    def __exit__(self, etype, value, traceback):
        os.chdir(self.savedPath)



linecounts = {}
linecounts[40] = 240012290
linecounts[60] = 360011594
linecounts[80] = 480025129
linecounts[100] = 600037902
linecounts[120] = 720040849
linecounts[140] = 840042983
linecounts[160] = 960017389
linecounts[180] = 1080017552
linecounts[200] = 1200018434

def transform(scale_factor):
    path = './'
    ip = '/mnt/hdd3/ssb-dbgen-{}/'.format (scale_factor) 
    op = '/mnt/hdd3/ssb-dbgen-{}/transformed/'.format (scale_factor) 
    with cd(path):
        os.system('mkdir -p %s' % op)
        os.system('python3 convert.py {}'.format(ip))
        os.system('./loader --lineorder %s/lineorder.tbl --ddate %s/date.tbl --customer %s/customer.tbl.p --supplier %s/supplier.tbl.p --part %s/part.tbl.p --datadir %s' % (ip, ip, ip, ip, ip, op))

def linecount(scale_factor):
    path = '/mnt/hdd3/ssb-dbgen-{}/transformed/'.format (scale_factor) 
    with cd(path):
        os.system('wc -l lineorder.tbl')
        os.system('wc -l part.tbl')
        os.system('wc -l supplier.tbl')
        os.system('wc -l customer.tbl')
        os.system('wc -l date.tbl')
        
def sort(scale_factor):
    path = './'
    ip = '/mnt/hdd3/ssb-dbgen-{}/'.format (scale_factor) 
    op = '/mnt/hdd3/ssb-dbgen-{}/transformed/'.format (scale_factor) 
    sortop = '/mnt/hdd3/ssb-dbgen-{}/columnar/'.format (scale_factor) 
    with cd(path):
        os.system ("./columnSort {}/LINEORDER {}/LINEORDERSORT 5 16 {}".format (op, op, linecounts[scale_factor]))

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description = 'data gen')
    # parser.add_argument('--dataset', type=str, choices=['ssb'])
    parser.add_argument('--sf', type=int)
    parser.add_argument('--action', type=str)
    # parser.add_argument('action', type=str, choices=['gen', 'transform'])
    args = parser.parse_args()

    if args.action == 'transform':
        transform(args.sf)
    elif args.action == 'linecount':
        linecount (args.sf)
    elif args.action == 'sort':
        sort (args.sf)

'''
for i in 40 60 80 100 120 140 160 180 200;
do
    for t in part supplier customer date;
    do 
        wc -l ssb-dbgen-${i}/${t}.tbl;
    done
done
'''

'''
for i in 40 60 80 100 120 140 160 180 200;
do
    cd ssb-dbgen-${i};
    scp *.tbl chaemin@192.168.0.34:/mnt/hdd3/ssb-dbgen-${i}/;
    cd /mnt/hdd;
done
'''
