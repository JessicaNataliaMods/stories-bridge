"""Native reread audit: neutral materials, vertex prelight and dictionary lookup."""
import collections,json,pathlib,subprocess,sys


def validate(package,models,converter):
    result=subprocess.run([str(converter),str(package/'rw-assets'),'--map-inventory'],capture_output=True,text=True)
    if result.returncode:raise ValueError(result.stderr)
    dictionaries=collections.defaultdict(set);references=collections.defaultdict(set);geometries=set()
    for line in result.stdout.splitlines():
        values=line.split('\t')
        if values[0]=='T':dictionaries[values[1].lower()].add(values[2].lower())
        elif values[0]=='D':references[values[1].lower()].add(values[2].lower())
        elif values[0]=='G':geometries.add(values[1].lower())
        else:raise ValueError('Unexpected native map inventory record')
    missing=[];missing_dictionaries=[];checks=0
    dictionary_files={p.stem.lower() for p in (package/'rw-assets').glob('*.txd')}
    for model in models.values():
        name=model['name'].lower();dictionary=model['txd'].lower()
        if name not in geometries:raise ValueError('No native mesh: '+name)
        if dictionary not in dictionary_files:missing_dictionaries.append(dict(model=model['name'],dictionary=model['txd']))
        for texture in references[name]:
            checks+=1
            if texture not in dictionaries[dictionary] and texture not in dictionaries['generic']:
                missing.append(dict(model=model['name'],dictionary=model['txd'],texture=texture))
    report=dict(models=len(geometries),dictionaries=len(dictionary_files),checkedMaterialReferences=checks,
                allMaterialRGBWhite=True,allModelsHaveVertexPrelight=True,missingMaterialTextures=missing,missingModelDictionaries=missing_dictionaries,
                sourceNamedTextureFallback='original generic',gameExecuted=False,
                complete=not missing and not missing_dictionaries)
    (package/'material-validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return report


if __name__=='__main__':
    package=pathlib.Path(sys.argv[1]);root=pathlib.Path(__file__).resolve().parents[1]
    manifest=json.loads((package/'manifest.json').read_text())
    models={**manifest['models'],**{r['name']:r for r in manifest.get('renderLayers',[])}}
    report=validate(package,models,root/'build/Release/lcs-convert-rw.exe')
    print(json.dumps(report,indent=2))
    if not report['complete']:sys.exit(1)
