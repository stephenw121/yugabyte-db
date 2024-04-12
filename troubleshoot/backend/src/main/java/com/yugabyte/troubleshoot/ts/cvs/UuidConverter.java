/*
 * Copyright 2007 the original author or authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.yugabyte.troubleshoot.ts.cvs;

import java.util.UUID;
import net.sf.jsefa.common.converter.SimpleTypeConverter;
import net.sf.jsefa.common.converter.SimpleTypeConverterConfiguration;
import org.apache.commons.lang3.StringUtils;

public class UuidConverter implements SimpleTypeConverter {

  public static UuidConverter create(SimpleTypeConverterConfiguration configuration) {
    return new UuidConverter(configuration);
  }

  protected UuidConverter(SimpleTypeConverterConfiguration configuration) {}

  /** {@inheritDoc} */
  public final synchronized UUID fromString(String value) {
    if (StringUtils.isBlank(value)) {
      return null;
    }
    return UUID.fromString(value);
  }

  public final synchronized String toString(Object value) {
    if (value == null) {
      return null;
    }
    return ((UUID) value).toString();
  }
}
