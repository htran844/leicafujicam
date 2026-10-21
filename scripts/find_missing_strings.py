import os
import xml.etree.ElementTree as ET

def get_strings(file_path):
    if not os.path.exists(file_path):
        return {}
    try:
        tree = ET.parse(file_path)
        root = tree.getroot()
        strings = {}
        for string_elem in root.findall('string'):
            name = string_elem.get('name')
            text = string_elem.text if string_elem.text else ""
            strings[name] = text
        return strings
    except Exception as e:
        print(f"Error parsing {file_path}: {e}")
        return {}

source_path = r'/values/strings.xml'
source_strings = get_strings(source_path)

locales = ['de', 'fr', 'in', 'ja', 'ko', 'pt-rBR', 'th', 'zh']
results = {}

for locale in locales:
    target_path = f'c:\\Users\\Hinnka\\Projects\\PhotonCamera\\app\\src\\main\\res\\values-{locale}\\strings.xml'
    target_strings = get_strings(target_path)
    
    missing = []
    for name in source_strings:
        if name not in target_strings:
            missing.append((name, source_strings[name]))
    
    results[locale] = missing

for locale, missing in results.items():
    print(f"--- {locale} ({len(missing)} missing keywords) ---")
    for name, text in missing:
        print(f"{name}: {text}")
