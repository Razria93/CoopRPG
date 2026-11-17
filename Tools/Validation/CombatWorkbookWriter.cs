using System;
using System.IO;
using System.IO.Compression;
using System.Text;
using System.Text.RegularExpressions;
using System.Xml;
using System.Globalization;

namespace CoopRPG.Validation
{
    // A small, text-only export of observed logs. No Excel/COM or external package dependency.
    public static class CombatWorkbookWriter
    {
        private const string Main = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
        private const string Rel = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";

        private static string ExcelText(string value)
        {
            value = value ?? "";
            if (value.Length > 32767)
                throw new ArgumentException("Excel cell exceeds 32767 characters. Export cancelled; original log preserved.");
            // Preserve literal escape-looking text and XML-incompatible UTF-16 characters.
            value = Regex.Replace(value, "_(?=x[0-9A-Fa-f]{4}_)", "_x005F_");
            var result = new StringBuilder();
            for (int i = 0; i < value.Length; i++)
            {
                char c = value[i];
                if (Char.IsHighSurrogate(c) && i + 1 < value.Length && Char.IsLowSurrogate(value[i + 1]))
                { result.Append(c); result.Append(value[++i]); }
                else if ((c < 32 && c != '\n' && c != '\t') || Char.IsSurrogate(c) || c == 0xFFFE || c == 0xFFFF)
                    result.Append("_x" + ((int)c).ToString("X4", CultureInfo.InvariantCulture) + "_");
                else result.Append(c);
            }
            return result.ToString();
        }

        private static string Column(int index)
        {
            string result = "";
            for (int n = index + 1; n > 0; n = (n - 1) / 26) result = (char)('A' + (n - 1) % 26) + result;
            return result;
        }

        private static XmlWriter Writer(ZipArchive zip, string path)
        {
            return XmlWriter.Create(zip.CreateEntry(path, CompressionLevel.Fastest).Open(),
                new XmlWriterSettings { Encoding = new UTF8Encoding(false), CloseOutput = true });
        }

        private static void Part(ZipArchive zip, string path, string xml)
        {
            using (var writer = new StreamWriter(zip.CreateEntry(path).Open(), new UTF8Encoding(false))) writer.Write(xml);
        }

        private static void Cell(XmlWriter x, string address, string value, int style, bool numeric)
        {
            x.WriteStartElement("c", Main); x.WriteAttributeString("r", address); x.WriteAttributeString("s", style.ToString());
            if (numeric)
            {
                long number;
                if (!Int64.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out number))
                    throw new ArgumentException("Invalid numeric log position.");
                x.WriteElementString("v", Main, number.ToString(CultureInfo.InvariantCulture));
            }
            else
            {
                x.WriteAttributeString("t", "inlineStr"); x.WriteStartElement("is", Main); x.WriteStartElement("t", Main);
                x.WriteAttributeString("xml", "space", "http://www.w3.org/XML/1998/namespace", "preserve");
                x.WriteString(ExcelText(value)); x.WriteEndElement(); x.WriteEndElement();
            }
            x.WriteEndElement();
        }

        private static void Sheet(ZipArchive zip, int id, string[] headers, string[][] rows, bool logs)
        {
            using (var x = Writer(zip, "xl/worksheets/sheet" + id + ".xml"))
            {
                x.WriteStartDocument(); x.WriteStartElement("worksheet", Main);
                x.WriteStartElement("dimension", Main); x.WriteAttributeString("ref", "A1:" + Column(headers.Length - 1) + (rows.Length + 1)); x.WriteEndElement();
                x.WriteStartElement("sheetViews", Main); x.WriteStartElement("sheetView", Main);
                x.WriteAttributeString("workbookViewId", "0"); x.WriteAttributeString("showGridLines", "0");
                x.WriteStartElement("pane", Main); x.WriteAttributeString("ySplit", "1");
                if (logs) x.WriteAttributeString("xSplit", "3");
                x.WriteAttributeString("topLeftCell", logs ? "D2" : "A2");
                x.WriteAttributeString("activePane", logs ? "bottomRight" : "bottomLeft"); x.WriteAttributeString("state", "frozen"); x.WriteEndElement();
                x.WriteStartElement("selection", Main); x.WriteAttributeString("pane", logs ? "bottomRight" : "bottomLeft");
                x.WriteAttributeString("activeCell", logs ? "D2" : "A2"); x.WriteAttributeString("sqref", logs ? "D2" : "A2"); x.WriteEndElement();
                x.WriteEndElement(); x.WriteEndElement();
                x.WriteStartElement("sheetFormatPr", Main); x.WriteAttributeString("defaultRowHeight", "32"); x.WriteEndElement();
                double[] widths = logs ? new double[] {8, 17, 17, 16, 12, 64, 22, 40, 40, 29, 30, 15, 30, 70, 12, 12, 16, 90, 110, 20, 22, 85} : new double[] {28, 120};
                x.WriteStartElement("cols", Main);
                for (int i = 0; i < headers.Length; i++)
                {
                    x.WriteStartElement("col", Main); x.WriteAttributeString("min", (i + 1).ToString()); x.WriteAttributeString("max", (i + 1).ToString());
                    x.WriteAttributeString("width", widths[i].ToString(CultureInfo.InvariantCulture)); x.WriteAttributeString("customWidth", "1"); x.WriteEndElement();
                }
                x.WriteEndElement(); x.WriteStartElement("sheetData", Main);
                for (int r = 0; r <= rows.Length; r++)
                {
                    x.WriteStartElement("row", Main); x.WriteAttributeString("r", (r + 1).ToString());
                    x.WriteAttributeString("ht", r == 0 ? "26" : "32"); x.WriteAttributeString("customHeight", "1");
                    string[] values = r == 0 ? headers : rows[r - 1];
                    for (int c = 0; c < headers.Length; c++)
                        Cell(x, Column(c) + (r + 1), values[c], r == 0 ? 1 : (r % 2 == 0 ? 3 : 2), logs && r > 0 && (c == 0 || c == 14 || c == 15 || c == 16));
                    x.WriteEndElement();
                }
                x.WriteEndElement();
                x.WriteStartElement("autoFilter", Main); x.WriteAttributeString("ref", "A1:" + Column(headers.Length - 1) + (rows.Length + 1)); x.WriteEndElement();
                x.WriteEndElement(); x.WriteEndDocument();
            }
        }

        public static void Write(string path, string[] headers, string[][] rows, string[][] metadata)
        {
            // Validate before opening any output. Existing paths are never overwritten.
            foreach (var row in rows) { if (row.Length != headers.Length) throw new ArgumentException("Column count mismatch."); foreach (var cell in row) ExcelText(cell); }
            foreach (var row in metadata) { if (row.Length != 2) throw new ArgumentException("Metadata column count mismatch."); foreach (var cell in row) ExcelText(cell); }
            bool created = false;
            try
            {
                using (var file = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                {
                    created = true;
                    using (var zip = new ZipArchive(file, ZipArchiveMode.Create))
                    {
                        Part(zip, "[Content_Types].xml", "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/><Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/><Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/><Override PartName=\"/xl/worksheets/sheet2.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/></Types>");
                        Part(zip, "_rels/.rels", "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"" + Rel + "/officeDocument\" Target=\"xl/workbook.xml\"/></Relationships>");
                        Part(zip, "xl/workbook.xml", "<workbook xmlns=\"" + Main + "\" xmlns:r=\"" + Rel + "\"><bookViews><workbookView activeTab=\"0\"/></bookViews><sheets><sheet name=\"로그\" sheetId=\"1\" r:id=\"rId1\"/><sheet name=\"내보내기 정보\" sheetId=\"2\" r:id=\"rId2\"/></sheets></workbook>");
                        Part(zip, "xl/_rels/workbook.xml.rels", "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"" + Rel + "/worksheet\" Target=\"worksheets/sheet1.xml\"/><Relationship Id=\"rId2\" Type=\"" + Rel + "/worksheet\" Target=\"worksheets/sheet2.xml\"/><Relationship Id=\"rId3\" Type=\"" + Rel + "/styles\" Target=\"styles.xml\"/></Relationships>");
                        Part(zip, "xl/styles.xml", "<styleSheet xmlns=\"" + Main + "\"><fonts count=\"2\"><font><sz val=\"11\"/><color rgb=\"FF202938\"/><name val=\"Malgun Gothic\"/></font><font><b/><sz val=\"11\"/><color rgb=\"FFFFFFFF\"/><name val=\"Malgun Gothic\"/></font></fonts><fills count=\"4\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill><fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF28303B\"/><bgColor indexed=\"64\"/></patternFill></fill><fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFF0F3F7\"/><bgColor indexed=\"64\"/></patternFill></fill></fills><borders count=\"1\"><border/></borders><cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs><cellXfs count=\"4\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/><xf fontId=\"1\" fillId=\"2\" borderId=\"0\" numFmtId=\"0\" xfId=\"0\" applyAlignment=\"1\" applyFont=\"1\" applyFill=\"1\"><alignment horizontal=\"center\" vertical=\"center\"/></xf><xf fontId=\"0\" fillId=\"0\" borderId=\"0\" numFmtId=\"0\" xfId=\"0\" applyAlignment=\"1\"><alignment vertical=\"top\" wrapText=\"1\"/></xf><xf fontId=\"0\" fillId=\"3\" borderId=\"0\" numFmtId=\"0\" xfId=\"0\" applyFill=\"1\" applyAlignment=\"1\"><alignment vertical=\"top\" wrapText=\"1\"/></xf></cellXfs><cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles></styleSheet>");
                        Sheet(zip, 1, headers, rows, true);
                        Sheet(zip, 2, new string[] { "항목", "값" }, metadata, false);
                    }
                }
            }
            catch { if (created) File.Delete(path); throw; }
        }
    }
}
