"""Development only: publish names/hashes, never reference texture pixels."""
import collections,json,pathlib

root=pathlib.Path(__file__).resolve().parents[1]
records=[]
for line in (root/'reports/reference-texture-fingerprints.tsv').read_text(encoding='utf-8-sig').splitlines():
    dictionary,name,w,h,sha=line.split('\t')
    if not name or len(name.encode('ascii'))>31 or len(sha)!=64:raise ValueError('Invalid name fingerprint')
    records.append(dict(dictionary=dictionary,name=name,width=int(w),height=int(h),sha256=sha))
materials=collections.defaultdict(list)
for line in (root/'reports/reference-model-material-names.tsv').read_text(encoding='utf-8-sig').splitlines():
    model,texture=line.split('\t');materials[model.lower()].append(texture)
metadata=dict(format=1,containsPixels=False,
              source='Development reference names accepted only on exact decoded PS2 dimension/RGBA match',
              textures=records,modelMaterials=materials)
(root/'local/metadata/ps2-texture-name-metadata.json').write_text(json.dumps(metadata,separators=(',',':')),encoding='utf-8')
print('Texture name fingerprints:',len(records),'model material sets:',len(materials),'pixel data included: false')
