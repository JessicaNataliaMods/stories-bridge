"""Recover original sector texture names by exact source raster correspondence."""
import collections
import hashlib
import struct
import json
import pathlib


def image_key(image):
    w,h,pixels=image
    return hashlib.sha256(struct.pack('<2I',w,h)+pixels).hexdigest()


class TextureNames:
    def __init__(self,game):
        self.records=[]
        self.by_pixels=collections.defaultdict(list)
        self.by_dictionary=collections.defaultdict(dict)
        for texture in game.named_textures():
            image=texture.pop('image');fingerprint=image_key(image)
            record=dict(**texture,width=image[0],height=image[1],sha256=fingerprint)
            self.records.append(record);self.by_pixels[fingerprint].append(record)
            self.by_dictionary[record['dictionary']][record['name']]=image
        self.model_materials={}
        metadata=pathlib.Path(__file__).with_name('..').resolve()/'local/metadata/ps2-texture-name-metadata.json'
        self.metadata_records=0
        if metadata.exists():
            table=json.loads(metadata.read_text(encoding='utf-8'))
            if table.get('format')!=1 or table.get('containsPixels') is not False:raise ValueError('Invalid texture-name metadata')
            self.model_materials={model.lower():set(names) for model,names in table['modelMaterials'].items()}
            for record in table['textures']:
                if not record['name'] or len(record['name'].encode('ascii'))>31:raise ValueError('Invalid texture alias')
                self.by_pixels[record['sha256']].append(dict(**record,referenceMetadata=True))
                self.metadata_records+=1

    def identify(self,image,dictionary,model=None):
        candidates=self.by_pixels.get(image_key(image),[])
        scoped=[x for x in candidates if x['dictionary'].lower()==dictionary.lower()]
        names={x['name'] for x in scoped or candidates}
        model_names=names & self.model_materials.get((model or '').lower(),set())
        if model_names:names=model_names
        if len(names)==1:
            name=next(iter(names))
            assigned=self.by_dictionary.get(dictionary,{}).get(name)
            return dict(name=name,method='exact original raster',
                        dictionaryMatch=bool(scoped),candidates=candidates,aliases=[],
                        dictionaryConflict=assigned is not None and image_key(assigned)!=image_key(image))
        if names:
            # Console sectors discarded the RslTexture name. Multiple original
            # names can refer to the exact same image; retain every such alias
            # in its dictionary and identify the chosen canonical name openly.
            return dict(name=sorted(names)[0],method='exact raster with equivalent original aliases',
                        dictionaryMatch=bool(scoped),candidates=candidates,aliases=sorted(names),
                        originalAliasAmbiguous=True)
        return dict(name=None,method='ambiguous original names' if names else 'no named raster match',
                    dictionaryMatch=False,candidates=candidates,aliases=[])


class TextureDictionaries:
    """Keep real texture names when resident and compiled rasters differ.

    A TXD cannot contain two different images under the same name. Preserve
    the original resident dictionary, and route compiled models to a separate
    dictionary only when necessary. All pixels still come from the PS2 input.
    """
    def __init__(self,original):
        self.original=original
        self.textures={}
        self.variants={}

    @staticmethod
    def compatible(existing,requested):
        images={name.lower():image_key(image) for name,image in existing.items()}
        return all(name.lower() not in images or images[name.lower()]==image_key(image)
                   for name,image in requested.items())

    def assign(self,dictionary,requested):
        resident=self.original.get(dictionary,{})
        current=self.textures.get(dictionary,{})
        if self.compatible(resident,requested) and self.compatible(current,requested):
            target=dictionary
        else:
            target=None
            for name,variant in self.variants.items():
                if variant['sourceDictionary']==dictionary and self.compatible(self.textures[name],requested):
                    target=name;break
            if target is None:
                i=1
                while True:
                    suffix='_world'+(str(i) if i>1 else '')
                    name=dictionary[:31-len(suffix)]+suffix
                    if name.lower() not in {x.lower() for x in (*self.original,*self.textures)}:break
                    i+=1
                target=name
                # Retain original resident entries except the exact names
                # replaced by different compiled rasters in this variant.
                self.textures[target]={n:im for n,im in resident.items()
                    if self.compatible({n:im},requested)}
                self.variants[target]=dict(sourceDictionary=dictionary,conflicts=[],models=[])
            variant=self.variants[target]
            existing={n.lower():(n,im) for n,im in resident.items()}
            for name,image in requested.items():
                original=existing.get(name.lower())
                if original and image_key(original[1])!=image_key(image):
                    conflict=dict(name=name,residentSHA256=image_key(original[1]),compiledSHA256=image_key(image))
                    if conflict not in variant['conflicts']:variant['conflicts'].append(conflict)
        self.textures.setdefault(target,{}).update(requested)
        return target

    def add_resident(self,dictionary,records):
        current=self.textures.setdefault(dictionary,{})
        if not self.compatible(current,records):raise ValueError('Resident texture conflict: '+dictionary)
        current.update(records)
